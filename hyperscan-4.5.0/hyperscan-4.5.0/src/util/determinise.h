/*
 * Copyright (c) 2015, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/** \file
 * \brief DFA determinisation code.
 */

#ifndef DETERMINISE_H
#define DETERMINISE_H

#include "nfagraph/ng_holder.h"
#include "charreach.h"
#include "container.h"
#include "ue2common.h"

#include <array>
#include <algorithm>
#include <vector>

namespace ue2 {

#define DETERMINISE_RESERVE_SIZE 10

/* Automaton details:
 *
 * const vector<StateSet> initial()
 *     returns initial states to start determinising from. StateSets in the
 *     initial() vector will given consecutive ids starting from 1, in the order
 *     that they appear.
 *
 *  void reports(StateSet s, flat_set<ReportID> *out)
 *     fills out with any reports that need to be raised for stateset.
 *
 *  void reportsEod(StateSet s, flat_set<ReportID> *out)
 *     fills out with any reports that need to be raised for stateset at EOD.
 *
 *  void transition(const StateSet &in, StateSet *next)
 *     fills the next array such next[i] is the stateset that in transitions to
 *     on seeing symbol i (i is in the compressed alphabet of the automaton).
 *
 *  u16 alphasize
 *     size of the compressed alphabet
 */

/** \brief determinises some sort of nfa
 *  \param n the automaton to determinise
 *  \param dstates_out output dfa states
 *  \param state_limit limit on the number of dfa states to construct
 *  \param statesets_out a mapping from DFA state to the set of NFA states in
 *         the automaton
 *  \return zero on success
 */
template<class Auto, class ds>
never_inline
int determinise(Auto &n, std::vector<ds> &dstates_out, dstate_id_t state_limit,
                std::vector<typename Auto::StateSet> *statesets_out = nullptr) {
    DEBUG_PRINTF("the determinator\n");
    typedef typename Auto::StateSet StateSet;
    typedef typename Auto::StateMap DstateIdMap;
    DstateIdMap dstate_ids;
    std::vector<StateSet> statesets;

    const size_t alphabet_size = n.alphasize;

    std::vector<ds> dstates;
    dstates.reserve(DETERMINISE_RESERVE_SIZE);
    statesets.reserve(DETERMINISE_RESERVE_SIZE);

    dstate_ids[n.dead] = DEAD_STATE;
    dstates.push_back(ds(alphabet_size));
    std::fill_n(dstates[0].next.begin(), alphabet_size, DEAD_STATE);

    statesets.push_back(n.dead);

    const std::vector<StateSet> &init = n.initial();
    for (u32 i = 0; i < init.size(); i++) {
        statesets.push_back(init[i]);
        assert(!contains(dstate_ids, init[i]));
        dstate_ids[init[i]] = dstates.size();
        dstates.push_back(ds(alphabet_size));
    }

    std::vector<StateSet> succs(alphabet_size, n.dead);
    for (dstate_id_t curr_id = DEAD_STATE; curr_id < dstates.size();
         curr_id++) {
        StateSet &curr = statesets[curr_id];

        DEBUG_PRINTF("curr: %hu\n", curr_id);

        /* fill in accepts */
        n.reports(curr, dstates[curr_id].reports);
        n.reportsEod(curr, dstates[curr_id].reports_eod);

        if (!dstates[curr_id].reports.empty()) {
            DEBUG_PRINTF("curr: %hu: is accept\n", curr_id);
        }

        if (!dstates[curr_id].reports.empty()) {
            /* only external reports set ekeys */
            if (n.canPrune(dstates[curr_id].reports)) {
                /* we only transition to dead on characters, TOPs leave us
                 * alone */
                std::fill_n(dstates[curr_id].next.begin(), alphabet_size,
                            DEAD_STATE);
                dstates[curr_id].next[n.alpha[TOP]] = curr_id;
                continue;
            }
        }

        /* fill in successor states */
        n.transition(curr, &succs[0]);
        for (symbol_t s = 0; s < n.alphasize; s++) {
            dstate_id_t succ_id;
            if (s && succs[s] == succs[s - 1]) {
                succ_id = dstates[curr_id].next[s - 1];
            } else {
                typename DstateIdMap::const_iterator dstate_id_iter;
                dstate_id_iter = dstate_ids.find(succs[s]);

                if (dstate_id_iter != dstate_ids.end()) {
                    succ_id = dstate_id_iter->second;

                    if (succ_id > curr_id && !dstates[succ_id].daddy
                        && n.unalpha[s] < N_CHARS) {
                        dstates[succ_id].daddy = curr_id;
                    }
                } else {
                    statesets.push_back(succs[s]);
                    succ_id = dstates.size();
                    dstate_ids[succs[s]] = succ_id;
                    dstates.push_back(ds(alphabet_size));
                    dstates.back().daddy = n.unalpha[s] < N_CHARS ? curr_id : 0;
                }

                DEBUG_PRINTF("-->%hu on %02hx\n", succ_id, n.unalpha[s]);
            }

            if (succ_id >= state_limit) {
                DEBUG_PRINTF("succ_id %hu >= state_limit %hu\n",
                             succ_id, state_limit);
                return -2;
            }

            dstates[curr_id].next[s] = succ_id;
        }
    }

    dstates_out = dstates;
    if (statesets_out) {
        statesets_out->swap(statesets);
    }
    DEBUG_PRINTF("ok\n");
    return 0;
}

static inline
std::vector<CharReach> populateCR(const NGHolder &g,
                                  const std::vector<NFAVertex> &v_by_index,
                                  const std::array<u16, ALPHABET_SIZE> &alpha) {
    std::vector<CharReach> cr_by_index(v_by_index.size());

    for (size_t i = 0; i < v_by_index.size(); i++) {
        const CharReach &cr = g[v_by_index[i]].char_reach;
        CharReach &cr_out = cr_by_index[i];
        for (size_t s = cr.find_first(); s != cr.npos; s = cr.find_next(s)) {
            cr_out.set(alpha[s]);
        }
    }

    return cr_by_index;
}

} // namespace ue2

#endif
