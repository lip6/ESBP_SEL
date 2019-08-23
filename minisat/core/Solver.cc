/***************************************************************************************[Solver.cc]
Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
Copyright (c) 2007-2010, Niklas Sorensson

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**************************************************************************************************/

#include <math.h>

#include "minisat/mtl/Alg.h"
#include "minisat/mtl/Sort.h"
#include "minisat/utils/System.h"
#include "minisat/core/Solver.h"

using namespace Minisat;

//=================================================================================================
// Options:


static const char* _cat = "CORE";

static DoubleOption  opt_var_decay         (_cat, "var-decay",   "The variable activity decay factor",            0.95,     DoubleRange(0, false, 1, false));
static DoubleOption  opt_clause_decay      (_cat, "cla-decay",   "The clause activity decay factor",              0.999,    DoubleRange(0, false, 1, false));
static DoubleOption  opt_random_var_freq   (_cat, "rnd-freq",    "The frequency with which the decision heuristic tries to choose a random variable", 0, DoubleRange(0, true, 1, true));
static DoubleOption  opt_random_seed       (_cat, "rnd-seed",    "Used by the random variable selection",         91648253, DoubleRange(0, false, HUGE_VAL, false));
static IntOption     opt_ccmin_mode        (_cat, "ccmin-mode",  "Controls conflict clause minimization (0=none, 1=basic, 2=deep)", 2, IntRange(0, 2));
static IntOption     opt_phase_saving      (_cat, "phase-saving", "Controls the level of phase saving (0=none, 1=limited, 2=full)", 2, IntRange(0, 2));
static BoolOption    opt_rnd_init_act      (_cat, "rnd-init",    "Randomize the initial activity", false);
static BoolOption    opt_luby_restart      (_cat, "luby",        "Use the Luby restart sequence", true);
static IntOption     opt_restart_first     (_cat, "rfirst",      "The base restart interval", 100, IntRange(1, INT32_MAX));
static DoubleOption  opt_restart_inc       (_cat, "rinc",        "Restart interval increase factor", 2, DoubleRange(1, false, HUGE_VAL, false));
static DoubleOption  opt_garbage_frac      (_cat, "gc-frac",     "The fraction of wasted memory allowed before a garbage collection is triggered",  0.20, DoubleRange(0, false, HUGE_VAL, false));
static IntOption     opt_min_learnts_lim   (_cat, "min-learnts", "Minimum learnt clause limit",  0, IntRange(0, INT32_MAX));

static BoolOption    opt_stop_prop      ("SYM", "stop-prop",    "Stop propagate if ESBP was found", false);
//=================================================================================================
// Constructor/Destructor:


Solver::Solver() :

    // Parameters (user settable):
    //
    symmetry(nullptr)
  , adapter(nullptr)
  , verbosity        (0)
  , var_decay        (opt_var_decay)
  , clause_decay     (opt_clause_decay)
  , random_var_freq  (opt_random_var_freq)
  , random_seed      (opt_random_seed)
  , luby_restart     (opt_luby_restart)
  , ccmin_mode       (opt_ccmin_mode)
  , phase_saving     (opt_phase_saving)
  , rnd_pol          (false)
  , rnd_init_act     (opt_rnd_init_act)
  , garbage_frac     (opt_garbage_frac)
  , min_learnts_lim  (opt_min_learnts_lim)
  , restart_first    (opt_restart_first)
  , restart_inc      (opt_restart_inc)

    // Parameters (the rest):
    //
  , learntsize_factor((double)1/(double)3), learntsize_inc(1.1)

    // Parameters (experimental):
    //
  , learntsize_adjust_start_confl (100)
  , learntsize_adjust_inc         (1.5)

    // Statistics: (formerly in 'SolverStats')
    //
  , solves(0), starts(0), decisions(0), rnd_decisions(0), propagations(0), conflicts(0)
  , dec_vars(0), num_clauses(0), num_learnts(0), clauses_literals(0), learnts_literals(0), max_literals(0), tot_literals(0)

  , watches            (WatcherDeleted(ca))
  , order_heap         (VarOrderLt(activity))
  , ok                 (true)
  , cla_inc            (1)
  , var_inc            (1)
  , qhead              (0)
  , simpDB_assigns     (-1)
  , simpDB_props       (0)
  , progress_estimate  (0)
  , remove_satisfied   (true)
  , next_var           (0)

    // Resource constraints:
    //
  , conflict_budget    (-1)
  , propagation_budget (-1)
  , asynch_interrupt   (false)
	, qhead_gen(0)
	, watchidx(0)
	, qhead_sel(0)
	, symgenprops(0)
	, symgenconfls(0)
	, symselprops(0)
	, symselconfls(0)
{
	selIdx.push(0);
	genWatchIndices.push(0);
}


Solver::~Solver()
{
	for(int i=0; i<generators.size(); ++i){
		delete generators[i];
	}
	for(int i=0; i<selClauseWatches.size(); ++i){
		delete selClauseWatches[i];
	}
}


//=================================================================================================
// Minor methods:


// Creates a new SAT variable in the solver. If 'decision' is cleared, variable will not be
// used as a decision variable (NOTE! This has effects on the meaning of a SATISFIABLE result).
//
Var Solver::newVar(lbool upol, bool dvar)
{
    Var v;
    if (free_vars.size() > 0){
        v = free_vars.last();
        free_vars.pop();
    }else
        v = next_var++;

    watches  .init(mkLit(v, false));
    watches  .init(mkLit(v, true ));
    assigns  .insert(v, l_Undef);
    vardata  .insert(v, mkVarData(CRef_Undef, 0));
    activity .insert(v, rnd_init_act ? drand(random_seed) * 0.00001 : 0);
    seen     .insert(v, 0);
    polarity .insert(v, true);
    user_pol .insert(v, upol);
    decision .reserve(v);
    trail    .capacity(v+1);
    setDecisionVar(v, dvar);
		selClauseWatches.push(new vec<int>());
		selClauseWatches.push(new vec<int>());
		genWatchIndices.push(genWatches.size());
    return v;
}


// Note: at the moment, only unassigned variable will be released (this is to avoid duplicate
// releases of the same variable).
void Solver::releaseVar(Lit l)
{
    if (value(l) == l_Undef){
        addClause(l);
        released_vars.push(var(l));
    }
}


bool Solver::addClause_(vec<Lit>& ps)
{
    assert(decisionLevel() == 0);
    if (!ok) return false;

    // Check if clause is satisfied and remove false/duplicate literals:
    sort(ps);
    Lit p; int i, j;
    for (i = j = 0, p = lit_Undef; i < ps.size(); i++)
        if (value(ps[i]) == l_True || ps[i] == ~p)
            return true;
        else if (value(ps[i]) != l_False && ps[i] != p)
            ps[j++] = p = ps[i];
    ps.shrink(i - j);

    if (ps.size() == 0)
        return ok = false;
    else if (ps.size() == 1){
        uncheckedEnqueue(ps[0]);
        return ok = (propagate() == CRef_Undef);
    }else{
        CRef cr = ca.alloc(ps, false, false, nullptr);
        clauses.push(cr);
        attachClause(cr);
    }

    return true;
}


void Solver::attachClause(CRef cr){
    const Clause& c = ca[cr];
    assert(c.size() > 1);
    watches[~c[0]].push(Watcher(cr, c[1]));
    watches[~c[1]].push(Watcher(cr, c[0]));
    if (c.learnt()) num_learnts++, learnts_literals += c.size();
    else            num_clauses++, clauses_literals += c.size();
}


void Solver::detachClause(CRef cr, bool strict){
    const Clause& c = ca[cr];
    assert(c.size() > 1);

    // Strict or lazy detaching:
    if (strict){
        remove(watches[~c[0]], Watcher(cr, c[1]));
        remove(watches[~c[1]], Watcher(cr, c[0]));
    }else{
        watches.smudge(~c[0]);
        watches.smudge(~c[1]);
    }

    if (c.learnt()) num_learnts--, learnts_literals -= c.size();
    else            num_clauses--, clauses_literals -= c.size();
}


void Solver::removeClause(CRef cr) {
    Clause& c = ca[cr];
    detachClause(cr);
    // Don't leave pointers to free'd memory!
    if (locked(c)) vardata[var(c[0])].reason = CRef_Undef;
    c.mark(1);
    ca.free(cr);
}


bool Solver::satisfied(const Clause& c) const {
    for (int i = 0; i < c.size(); i++)
        if (value(c[i]) == l_True)
            return true;
    return false; }


// Revert to the state at given level (keeping all assignment at 'level' but not beyond).
//
void Solver::cancelUntil(int lvl) {
    if (decisionLevel() > lvl){
        for (int c = trail.size()-1; c >= trail_lim[lvl]; c--){
            Var      x  = var(trail[c]);
            assigns [x] = l_Undef;
	    if (symmetry != nullptr)
                symmetry->updateCancel(trail[c]);
            if (phase_saving > 1 || (phase_saving == 1) && c > trail_lim.last())
                polarity[x] = sign(trail[c]);
            insertVarOrder(x); }
        qhead = trail_lim[lvl];
        qhead_gen = trail_lim[lvl];
        qhead_sel = trail_lim[lvl];
        watchidx = 0;
        trail.shrink(trail.size() - trail_lim[lvl]);
        trail_lim.shrink(trail_lim.size() - lvl);
		    if(lvl==0){
			    for(int i=0; i<selClauseWatches.size(); ++i){
				    selClauseWatches[i]->clear();
			    }
			    selClauses.clear();
			    selIdx.clear(); selIdx.push(0);
			    selGen.clear();
			    selProp.clear();
		    }else{
			    while(selProp.size()>0 && level(selProp.last())>lvl){
				    selProp.pop();
			    }
			    selGen.shrinkTo_(selProp.size());
			    selIdx.shrinkTo_(selProp.size()+1);
			    assert(selIdx.size()>0);
			    selClauses.shrinkTo_(selIdx.last());
		    }
    } }


//=================================================================================================
// Major methods:


Lit Solver::pickBranchLit()
{
    Var next = var_Undef;

    // Random decision:
    if (drand(random_seed) < random_var_freq && !order_heap.empty()){
        next = order_heap[irand(random_seed,order_heap.size())];
        if (value(next) == l_Undef && decision[next])
            rnd_decisions++; }

    // Activity based decision:
    while (next == var_Undef || value(next) != l_Undef || !decision[next])
        if (order_heap.empty()){
            next = var_Undef;
            break;
        }else
            next = order_heap.removeMin();

    // Choose polarity based on different polarity modes (global or per-variable):
    if (next == var_Undef)
        return lit_Undef;
    else if (user_pol[next] != l_Undef)
        return mkLit(next, user_pol[next] == l_True);
    else if (rnd_pol)
        return mkLit(next, drand(random_seed) < 0.5);
    else
        return mkLit(next, polarity[next]);
}


/*_________________________________________________________________________________________________
|
|  analyze : (confl : Clause*) (out_learnt : vec<Lit>&) (out_btlevel : int&)  ->  [void]
|
|  Description:
|    Analyze conflict and produce a reason clause.
|
|    Pre-conditions:
|      * 'out_learnt' is assumed to be cleared.
|      * Current decision level must be greater than root level.
|
|    Post-conditions:
|      * 'out_learnt[0]' is the asserting literal at level 'out_btlevel'.
|      * If out_learnt.size() > 1 then 'out_learnt[1]' has the greatest decision level of the
|        rest of literals. There may be others from the same level though.
|
|________________________________________________________________________________________________@*/
void Solver::analyze(CRef confl, vec<Lit>& out_learnt, int& out_btlevel, bool &outSym, std::set<SymGenerator*>* comp)
{
    int pathC = 0;
    Lit p     = lit_Undef;

    std::vector<std::set<SymGenerator*>*> symmetries;
    std::set<Lit> units;

    outSym = false;

    // Generate conflict clause:
    //
    out_learnt.push();      // (leave room for the asserting literal)
    int index   = trail.size() - 1;

    do{
        assert(confl != CRef_Undef); // (otherwise should be UIP)
        Clause& c = ca[confl];

        if (c.learnt())
            claBumpActivity(c);

        if (c.symmetry()) {
            outSym = true;
            assert(ca[confl].scompat() != nullptr);
            symmetries.push_back(ca[confl].scompat());
        }

        for (int j = (p == lit_Undef) ? 0 : 1; j < c.size(); j++){
            Lit q = c[j];

            if (level(var(q)) == 0 && (forbid_units.find(~q) != forbid_units.end())) {
                units.insert(q);
                outSym = true;
            }

            if (!seen[var(q)] && level(var(q)) > 0){
                varBumpActivity(var(q));
                seen[var(q)] = 1;
                if (level(var(q)) >= decisionLevel())
                    pathC++;
                else
                    out_learnt.push(q);
            }
        }

        // Select next clause to look at:
        while (!seen[var(trail[index--])]);
        p     = trail[index+1];
        confl = reason(var(p));
        seen[var(p)] = 0;
        pathC--;

    }while (pathC > 0);
    out_learnt[0] = ~p;

    // Simplify conflict clause:
    //
    int i, j;
    out_learnt.copyTo(analyze_toclear);
    if (ccmin_mode == 2){
        for (i = j = 1; i < out_learnt.size(); i++)
            if (reason(var(out_learnt[i])) == CRef_Undef || !litRedundant(out_learnt[i]))
                out_learnt[j++] = out_learnt[i];

    }else if (ccmin_mode == 1){
        for (i = j = 1; i < out_learnt.size(); i++){
            Var x = var(out_learnt[i]);

            if (reason(x) == CRef_Undef)
                out_learnt[j++] = out_learnt[i];
            else{
                Clause& c = ca[reason(var(out_learnt[i]))];
                for (int k = 1; k < c.size(); k++)
                    if (!seen[var(c[k])] && level(var(c[k])) > 0){
                        out_learnt[j++] = out_learnt[i];
                        break; }
            }
        }
    }else
        i = j = out_learnt.size();

    max_literals += out_learnt.size();
    out_learnt.shrink(i - j);
    tot_literals += out_learnt.size();

    // Find correct backtrack level:
    //
    if (out_learnt.size() == 1)
        out_btlevel = 0;
    else{
        int max_i = 1;
        // Find the first literal assigned at the next-highest level:
        for (int i = 2; i < out_learnt.size(); i++)
            if (level(var(out_learnt[i])) > level(var(out_learnt[max_i])))
                max_i = i;
        // Swap-in this literal at index 1:
        Lit p             = out_learnt[max_i];
        out_learnt[max_i] = out_learnt[1];
        out_learnt[1]     = p;
        out_btlevel       = level(var(p));
    }

    for (int j = 0; j < analyze_toclear.size(); j++) seen[var(analyze_toclear[j])] = 0;    // ('seen[]' is now cleared)

     if (!outSym)
        return;

    comp->clear();
    for (std::set<SymGenerator*>* check : symmetries) {
        if (check->empty()) {
            comp->clear();
            break;
        }

        if (comp->empty()) {
            comp->insert(check->begin(), check->end());
            continue;
        }

        // make intersection with c in place on comp
        std::set<SymGenerator*>::iterator it1 = comp->begin();
        std::set<SymGenerator*>::iterator it2 = check->begin();
        while ( (it1 != comp->end()) && (it2 != check->end()) ) {
            if (*it1 < *it2) {
                comp->erase(it1++);
            } else if (*it2 < *it1) {
                ++it2;
            } else { // *it1 == *it2
                ++it1;
                ++it2;
            }
        }
        comp->erase(it1, comp->end());
        if (comp->empty())
            break;
    }

    std::set<SymGenerator*> to_remove;
    for (Lit l : units) {
        assert(level(var(l)) == 0);
        for (SymGenerator *g : *comp) {
            Lit image = g->getImage(l);
            if (value(image) != value(l) || level(var(image)) != 0)
                to_remove.insert(g);
            // if (image != l)
            //     to_remove.insert(g);
        }
    }
    for (SymGenerator *g : to_remove) {
        comp->erase(g);
    }

    // Add stabilizer
    for (int i=0; i<generators.size(); i++) {
        SymGenerator *g = generators[i];
        if(comp->find(g) != comp->end())
            continue;

        if (g->stabilize(out_learnt))
            comp->insert(g);
    }
}


// Check if 'p' can be removed from a conflict clause.
bool Solver::litRedundant(Lit p)
{
    enum { seen_undef = 0, seen_source = 1, seen_removable = 2, seen_failed = 3 };
    assert(seen[var(p)] == seen_undef || seen[var(p)] == seen_source);
    assert(reason(var(p)) != CRef_Undef);

    Clause*               c     = &ca[reason(var(p))];
    vec<ShrinkStackElem>& stack = analyze_stack;
    stack.clear();

    bool isSym = false;

    if (c->symmetry())
        isSym = true;

    for (uint32_t i = 1; ; i++){
        if (i < (uint32_t)c->size()){
            // Checking 'p'-parents 'l':
            Lit l = (*c)[i];

            if (forbid_units.find(~l) != forbid_units.end())
                isSym = true;

            // Variable at level 0 or previously removable:
            if (level(var(l)) == 0 || seen[var(l)] == seen_source || seen[var(l)] == seen_removable){
                continue;
            }

            // Check variable can not be removed for some local reason:
            if (reason(var(l)) == CRef_Undef || seen[var(l)] == seen_failed){
                stack.push(ShrinkStackElem(0, p));
                for (int i = 0; i < stack.size(); i++)
                    if (seen[var(stack[i].l)] == seen_undef){
                        seen[var(stack[i].l)] = seen_failed;
                        analyze_toclear.push(stack[i].l);
                    }

                return false;
            }

            // Recursively check 'l':
            stack.push(ShrinkStackElem(i, p));
            i  = 0;
            p  = l;
            c  = &ca[reason(var(p))];

            if (c->symmetry())
                isSym = true;

        }else{
            // Finished with current element 'p' and reason 'c':
            if (seen[var(p)] == seen_undef){
                seen[var(p)] = seen_removable;
                analyze_toclear.push(p);
            }

            // Terminate with success if stack is empty:
            if (stack.size() == 0) break;

            // Continue with top element on stack:
            i  = stack.last().i;
            p  = stack.last().l;
            c  = &ca[reason(var(p))];

            if (c->symmetry())
                isSym = true;

            stack.pop();
        }
    }

    return !isSym;
}


/*_________________________________________________________________________________________________
|
|  analyzeFinal : (p : Lit)  ->  [void]
|
|  Description:
|    Specialized analysis procedure to express the final conflict in terms of assumptions.
|    Calculates the (possibly empty) set of assumptions that led to the assignment of 'p', and
|    stores the result in 'out_conflict'.
|________________________________________________________________________________________________@*/
void Solver::analyzeFinal(Lit p, LSet& out_conflict)
{
    out_conflict.clear();
    out_conflict.insert(p);

    if (decisionLevel() == 0)
        return;

    seen[var(p)] = 1;

    for (int i = trail.size()-1; i >= trail_lim[0]; i--){
        Var x = var(trail[i]);
        if (seen[x]){
            if (reason(x) == CRef_Undef){
                assert(level(x) > 0);
                out_conflict.insert(~trail[i]);
            }else{
                Clause& c = ca[reason(x)];
                for (int j = 1; j < c.size(); j++)
                    if (level(var(c[j])) > 0)
                        seen[var(c[j])] = 1;
            }
            seen[x] = 0;
        }
    }

    seen[var(p)] = 0;
}


void Solver::uncheckedEnqueue(Lit p, CRef from)
{
    assert(value(p) == l_Undef);
    assigns[var(p)] = lbool(!sign(p));
    vardata[var(p)] = mkVarData(from, decisionLevel());
    trail.push_(p);

    if (decisionLevel() == 0 && from != CRef_Undef) {
        const Clause& c = ca[from];
        if (c.symmetry()) {
            forbid_units.insert(p);
        } else {
            for (int i=0; i<c.size(); i++) {
                if (forbid_units.find(~c[i]) != forbid_units.end()) {
                    forbid_units.insert(p);
                    break;
                }
            }
        }
    }
}


/*_________________________________________________________________________________________________
|
|  propagate : [void]  ->  [Clause*]
|
|  Description:
|    Propagates all enqueued facts. If a conflict arises, the conflicting clause is returned,
|    otherwise CRef_Undef.
|
|    Post-conditions:
|      * the propagation queue is empty, even if there was a conflict.
|________________________________________________________________________________________________@*/
CRef Solver::propagate()
{
    CRef    confl     = CRef_Undef;
    int     num_props = 0;
StartPropagate:
    while (qhead < trail.size()){
        Lit            p   = trail[qhead++];     // 'p' is enqueued fact to propagate.
        vec<Watcher>&  ws  = watches.lookup(p);
        Watcher        *i, *j, *end;
        num_props++;

	if (symmetry) {
		symmetry->updateNotify(p);
	        CRef cr = learntSymmetryClause(cosy::ClauseInjector::ESBP, p);
                if (opt_stop_prop && cr != CRef_Undef) {
                    qhead = trail.size();
                    return cr;
                }
	}
        for (i = j = (Watcher*)ws, end = i + ws.size();  i != end;){
            // Try to avoid inspecting the clause:
            Lit blocker = i->blocker;
            if (value(blocker) == l_True){
                *j++ = *i++; continue; }

            // Make sure the false literal is data[1]:
            CRef     cr        = i->cref;
            Clause&  c         = ca[cr];
            Lit      false_lit = ~p;
            if (c[0] == false_lit)
                c[0] = c[1], c[1] = false_lit;
            assert(c[1] == false_lit);
            i++;

            // If 0th watch is true, then clause is already satisfied.
            Lit     first = c[0];
            Watcher w     = Watcher(cr, first);
            if (first != blocker && value(first) == l_True){
                *j++ = w; continue; }

            // Look for new watch:
            for (int k = 2; k < c.size(); k++)
                if (value(c[k]) != l_False){
                    c[1] = c[k]; c[k] = false_lit;
                    watches[~c[1]].push(w);
                    goto NextClause; }

            // Did not find watch -- clause is unit under assignment:
            *j++ = w;
            if (value(first) == l_False){
                confl = cr;
                qhead = trail.size();
                // Copy the remaining watches:
                while (i < end)
                    *j++ = *i++;
            }else
                uncheckedEnqueue(first, cr);

        NextClause:;
        }
        ws.shrink(i - j);
    }

		vec<Lit> symmetrical;
#if 1
	/*** first check existing symmetrical clauses ***/
		for(; confl == CRef_Undef && qhead_sel<trail.size(); ++qhead_sel){
			Lit prop = trail[qhead_sel];
			vec<int>& clWatches = *(selClauseWatches[toInt(prop)]);

			int watchedclause_i = 0;
			while(watchedclause_i < clWatches.size()){
				int currentclause = clWatches[watchedclause_i];
				if(currentclause >= selProp.size()){ // no more such clause, erase watch
					clWatches.swapErase(watchedclause_i);
					continue;
				}
				assert(selIdx.size()>currentclause+1);

				// find watched literal
				int c_start = selIdx[currentclause];
				if(value(selClauses[c_start])==l_True || value(selClauses[c_start+1])==l_True){ // either watched literal is true, ignore this satisfied clause
					// NOTE: the case where this watched literal is indeed true is due to lazily cleaning up the watched literals, so sometimes an uncleaned literal points to an existing opposed watch by accident
					++watchedclause_i;
					continue;
				}

				clWatches.swapErase(watchedclause_i); // all remaining cases will erase this watch
				int watch = 0;
				while(selClauses[c_start+watch]!=~prop && watch<2){
					++watch;
				}
				if(watch>=2){ // watched literal has become invalid (e.g. clause already added), erase watch
					continue;
				}
				// find new watch, otherwise unit or conflicting
				int c_end = selIdx[currentclause+1];
				watch += c_start; // now watch is the absolute index of the watch, instead of the index relative to c_start
				assert(value(selClauses[watch])==l_False);
				for(int i=c_start+2; i<c_end; ++i){
					if(value(selClauses[i])!=l_False){
						Lit tmp = selClauses[i];
						selClauses[i]=selClauses[watch];
						selClauses[watch]=tmp;
						break;
					}
				}
				if(value(selClauses[watch])!=l_False){ // new watch found, erase old, create new
					selClauseWatches[toInt(~selClauses[watch])]->push(currentclause);
					continue;
				}

				// unit or conflicting clause
				assert(value(selClauses[watch])==l_False);

				// create new learned clause
                                const Clause& original = ca[reason(selProp[currentclause])];

                                if (original.symmetry() && original.scompat()->find(selGen[currentclause]) == original.scompat()->end())
                                    continue;

				selGen[currentclause]->getSymmetricalClause(original, symmetrical);
				minimizeClause(symmetrical);
				if(symmetrical.size()<2){
					assert(symmetrical.size()==1);
					cancelUntil(0);
					if(value(symmetrical[0])==l_Undef){ // unit clause
						++symselprops;
						uncheckedEnqueue(symmetrical[0]);
						goto StartPropagate;
					} else if (value(symmetrical[0])==l_False){ // conflict clause
						++symselconfls;
						confl = CRef_Unsat;
						goto ConflDetected;
					}
				}
				assert(symmetrical.size()>1);
				prepareWatches(symmetrical);

				assert(value(symmetrical[1])==l_False);
				confl = addClauseFromSymmetry(original, symmetrical);
				if(confl==CRef_Undef){ // unit clause
					++symselprops;
					goto StartPropagate; // TODO: fix useless iteration over previous watches (i)
				}else{ // conflict clause
					++symselconfls;
					goto ConflDetected;
				}
			}
		}
#endif
	/*** check for new symmetrical clauses ***/
		for(; confl == CRef_Undef && qhead_gen<trail.size(); ++qhead_gen, watchidx=0){ // do generator symmetry propagation
			Lit currentGenLit = trail[qhead_gen];
			assert(level(var(currentGenLit))==decisionLevel());

			int watchStart = genWatchIndices[var(currentGenLit)];
			int watchEnd = genWatchIndices[var(currentGenLit)+1];

			if(level(var(currentGenLit))==0){ // NOTE: special purpose level 0 method needed as not all level 0 propagations have a reason clause attached to it
                            continue;
			}

			CRef reason_cgl = reason(var(currentGenLit));
			if(reason_cgl==CRef_Undef){
				continue; // choice literal
			}

			for(; watchidx<watchEnd-watchStart; ++watchidx){
				SymGenerator* g = genWatches[watchStart+watchidx];
				assert(g->permutes(currentGenLit));

                                if (ca[reason_cgl].symmetry() && ca[reason_cgl].scompat()->find(g) == ca[reason_cgl].scompat()->end())
                                    continue;

                                if (!ca[reason_cgl].symmetry())
                                    assert(ca[reason_cgl].scompat() == nullptr);
				int result = addSelClause(g, currentGenLit);
				if(result<2){ // either conflict or unit clause
					g->getSymmetricalClause(ca[reason_cgl],symmetrical);
					minimizeClause(symmetrical);
					if(symmetrical.size()<2){
						assert(symmetrical.size()==1);
                                                assert(false);
						cancelUntil(0);
						if(value(symmetrical[0])==l_Undef){ // unit clause
                                                    assert(false);
							++symgenprops;
							uncheckedEnqueue(symmetrical[0]);
							goto StartPropagate;
						} else if (value(symmetrical[0])==l_False){ // conflict clause
							++symgenconfls;
							confl = CRef_Unsat;
							goto ConflDetected;
						}
					}
					assert(symmetrical.size()>1);
					prepareWatches(symmetrical);
					assert(value(symmetrical[1])==l_False);
					confl = addClauseFromSymmetry(ca[reason_cgl], symmetrical);

					assert(result==0 || confl==CRef_Undef); // propagating result should lead to undef clause
					// NOTE: it is possible that (confl==CRef_Undef) & (result==0), if the symmetrical clause was a unit clause at some level, but has been made conflicting at a higher level. We treat this as a unit clause

					if(confl==CRef_Undef){ // unit clause
						++symgenprops;
						goto StartPropagate;
					}else{ // conflict clause
						++symgenconfls;
						goto ConflDetected;
					}
				}
			}
		}
		assert(testSelClauses());

ConflDetected:

    propagations += num_props;
    simpDB_props -= num_props;

    return confl;
}


/*_________________________________________________________________________________________________
|
|  reduceDB : ()  ->  [void]
|
|  Description:
|    Remove half of the learnt clauses, minus the clauses locked by the current assignment. Locked
|    clauses are clauses that are reason to some assignment. Binary clauses are never removed.
|________________________________________________________________________________________________@*/
struct reduceDB_lt {
    ClauseAllocator& ca;
    reduceDB_lt(ClauseAllocator& ca_) : ca(ca_) {}
    bool operator () (CRef x, CRef y) {
        return ca[x].size() > 2 && (ca[y].size() == 2 || ca[x].activity() < ca[y].activity()); }
};
void Solver::reduceDB()
{
    return;
    int     i, j;
    double  extra_lim = cla_inc / learnts.size();    // Remove any clause below this activity

    sort(learnts, reduceDB_lt(ca));
    // Don't delete binary or locked clauses. From the rest, delete clauses from the first half
    // and clauses with activity smaller than 'extra_lim':
    for (i = j = 0; i < learnts.size(); i++){
        Clause& c = ca[learnts[i]];
        if (c.size() > 2 && !locked(c) && (i < learnts.size() / 2 || c.activity() < extra_lim))
            removeClause(learnts[i]);
        else
            learnts[j++] = learnts[i];
    }
    learnts.shrink(i - j);
    checkGarbage();
}


void Solver::removeSatisfied(vec<CRef>& cs)
{
    int i, j;
    for (i = j = 0; i < cs.size(); i++){
        Clause& c = ca[cs[i]];
        if (satisfied(c))
            removeClause(cs[i]);
        else{
            // // Trim clause:
            // assert(value(c[0]) == l_Undef && value(c[1]) == l_Undef);
            // for (int k = 2; k < c.size(); k++)
            //     if (value(c[k]) == l_False){
            //         c[k--] = c[c.size()-1];
            //         c.pop();
            //     }
            // cs[j++] = cs[i];
        }
    }
    cs.shrink(i - j);
}


void Solver::rebuildOrderHeap()
{
    vec<Var> vs;
    for (Var v = 0; v < nVars(); v++)
        if (decision[v] && value(v) == l_Undef)
            vs.push(v);
    order_heap.build(vs);
}


/*_________________________________________________________________________________________________
|
|  simplify : [void]  ->  [bool]
|
|  Description:
|    Simplify the clause database according to the current top-level assigment. Currently, the only
|    thing done here is the removal of satisfied clauses, but more things can be put here.
|________________________________________________________________________________________________@*/
bool Solver::simplify()
{
    assert(decisionLevel() == 0);

    if (!ok || propagate() != CRef_Undef)
        return ok = false;

    if (nAssigns() == simpDB_assigns || (simpDB_props > 0))
        return true;

    // Remove satisfied clauses:
    removeSatisfied(learnts);
    if (remove_satisfied){       // Can be turned off.
        removeSatisfied(clauses);

        // TODO: what todo in if 'remove_satisfied' is false?

        // Remove all released variables from the trail:
        for (int i = 0; i < released_vars.size(); i++){
            assert(seen[released_vars[i]] == 0);
            seen[released_vars[i]] = 1;
        }

        int i, j;
        for (i = j = 0; i < trail.size(); i++)
            if (seen[var(trail[i])] == 0)
                trail[j++] = trail[i];
        trail.shrink(i - j);
        //printf("trail.size()= %d, qhead = %d\n", trail.size(), qhead);
        qhead = trail.size();

        for (int i = 0; i < released_vars.size(); i++)
            seen[released_vars[i]] = 0;

        // Released variables are now ready to be reused:
        append(released_vars, free_vars);
        released_vars.clear();
    }
    checkGarbage();
    rebuildOrderHeap();

    simpDB_assigns = nAssigns();
    simpDB_props   = clauses_literals + learnts_literals;   // (shouldn't depend on stats really, but it will do for now)

    return true;
}


/*_________________________________________________________________________________________________
|
|  search : (nof_conflicts : int) (params : const SearchParams&)  ->  [lbool]
|
|  Description:
|    Search for a model the specified number of conflicts.
|    NOTE! Use negative value for 'nof_conflicts' indicate infinity.
|
|  Output:
|    'l_True' if a partial assigment that is consistent with respect to the clauseset is found. If
|    all variables are decision variables, this means that the clause set is satisfiable. 'l_False'
|    if the clause set is unsatisfiable. 'l_Undef' if the bound on number of conflicts is reached.
|________________________________________________________________________________________________@*/
lbool Solver::search(int nof_conflicts)
{
    assert(ok);
    int         backtrack_level;
    int         conflictC = 0;
    vec<Lit>    learnt_clause;
    starts++;
    bool isSym = false;

    for (;;){
        CRef confl = propagate();
        if (confl != CRef_Undef){

            const Clause& c = ca[confl];
            for (int i=0; i<c.size(); i++) {
                if (value(c[i]) != l_False) {
                        std::cout << "== FATAL ERROR: False conflict " << std::endl;
                        exit(1);
                    }
            }

            // CONFLICT
            conflicts++; conflictC++;
            if (decisionLevel() == 0) return l_False;

            learnt_clause.clear();
            std::set<SymGenerator*> comp;
            analyze(confl, learnt_clause, backtrack_level, isSym, &comp);
            cancelUntil(backtrack_level);

            if (learnt_clause.size() == 1){
                uncheckedEnqueue(learnt_clause[0]);
                Lit l = learnt_clause[0];

                if (isSym) {
                    forbid_units.insert(l);

                    for (SymGenerator* g : comp) {
                        // TODO ADD WHOLE ORBITS
                        if (g->permutes(l)) {
                            Lit image = g->getImage(l);
                            if (value(image) == l_Undef)
                                uncheckedEnqueue(image);
                            else if (value(image) == l_False) {
                             std::cout << "UNSAT HERE" << std::endl;
                             return l_False;
                            }
                        }
                    }
                } else {
                    for (int i=0; i<generators.size(); i++) {
                        SymGenerator * g = generators[i];
                        // TODO ADD WHOLE ORBITS
                        if (g->permutes(l)) {
                            Lit image = g->getImage(l);
                            if (value(image) == l_Undef)
                                uncheckedEnqueue(image);
                            else if (value(image) == l_False)
                                return l_False;
                        }
                    }
                }
            }else{
                std::set<SymGenerator*>* valid = isSym ? new std::set<SymGenerator*>(comp.begin(), comp.end()) : nullptr;
                if (isSym)
                    assert(valid != nullptr);

                CRef cr = ca.alloc(learnt_clause, true, isSym, valid);
                learnts.push(cr);
                attachClause(cr);
                claBumpActivity(ca[cr]);
                uncheckedEnqueue(learnt_clause[0], cr);
            }

            varDecayActivity();
            claDecayActivity();

            if (--learntsize_adjust_cnt == 0){
                learntsize_adjust_confl *= learntsize_adjust_inc;
                learntsize_adjust_cnt    = (int)learntsize_adjust_confl;
                max_learnts             *= learntsize_inc;

                if (verbosity >= 1)
                    printf("| %9d | %7d %8d %8d | %8d %8d %6.0f | %6.3f %% |\n",
                           (int)conflicts,
                           (int)dec_vars - (trail_lim.size() == 0 ? trail.size() : trail_lim[0]), nClauses(), (int)clauses_literals,
                           (int)max_learnts, nLearnts(), (double)learnts_literals/nLearnts(), progressEstimate()*100);
            }

        }else{
            // NO CONFLICT
            if ((nof_conflicts >= 0 && conflictC >= nof_conflicts) || !withinBudget()){
                // Reached bound on number of conflicts:
                progress_estimate = progressEstimate();
                cancelUntil(0);
                return l_Undef; }

            // Simplify the set of problem clauses:
            if (decisionLevel() == 0 && !simplify())
                return l_False;

            if (learnts.size()-nAssigns() >= max_learnts)
                // Reduce the set of learnt clauses:
                reduceDB();

            Lit next = lit_Undef;
            while (decisionLevel() < assumptions.size()){
                // Perform user provided assumption:
                Lit p = assumptions[decisionLevel()];
                if (value(p) == l_True){
                    // Dummy decision level:
                    newDecisionLevel();
                }else if (value(p) == l_False){
                    analyzeFinal(~p, conflict);
                    return l_False;
                }else{
                    next = p;
                    break;
                }
            }

            if (next == lit_Undef){
                // New variable decision:
                decisions++;
                next = pickBranchLit();

                if (next == lit_Undef)
                    // Model found:
                    return l_True;
            }

            // Increase decision level and enqueue 'next'
            newDecisionLevel();
            uncheckedEnqueue(next);
        }
    }
}


double Solver::progressEstimate() const
{
    double  progress = 0;
    double  F = 1.0 / nVars();

    for (int i = 0; i <= decisionLevel(); i++){
        int beg = i == 0 ? 0 : trail_lim[i - 1];
        int end = i == decisionLevel() ? trail.size() : trail_lim[i];
        progress += pow(F, i) * (end - beg);
    }

    return progress / nVars();
}

/*
  Finite subsequences of the Luby-sequence:

  0: 1
  1: 1 1 2
  2: 1 1 2 1 1 2 4
  3: 1 1 2 1 1 2 4 1 1 2 1 1 2 4 8
  ...


 */

static double luby(double y, int x){

    // Find the finite subsequence that contains index 'x', and the
    // size of that subsequence:
    int size, seq;
    for (size = 1, seq = 0; size < x+1; seq++, size = 2*size+1);

    while (size-1 != x){
        size = (size-1)>>1;
        seq--;
        x = x % size;
    }

    return pow(y, seq);
}

// NOTE: assumptions passed in member-variable 'assumptions'.
lbool Solver::solve_()
{
    model.clear();
    conflict.clear();
    if (!ok) return l_False;

		if(solves==0){
			vec<double> occs;
			occs.growTo(2*nVars(),0.0);
			for(int i=0; i<nClauses(); ++i){
				const Clause &c = ca[clauses[i]];
				double increment = 1/(double)(c.size()*c.size());
				for(int j=0; j<c.size(); ++j){
					occs[toInt(c[j])]=occs[toInt(c[j])]+increment;
				}
			}
			for(int i=0; i<nVars(); ++i){
				polarity[i]=occs[2*i]<=occs[2*i+1]; // initial polarity is set to true if the negative lit occurs at least as much as the positive in the theory.
				activity[i]=occs[2*i]*occs[2*i+1];
			}
			rebuildOrderHeap();
		}

    solves++;
 // Set symmetry order
    if (symmetry != nullptr) {
        symmetry->enableCosy(cosy::OrderMode::AUTO,
                             cosy::ValueMode::TRUE_LESS_FALSE
                             // FALSE_LESS_TRUE
                             );
        symmetry->printInfo();

        notifyCNFUnits();

        cosy::ClauseInjector::Type type = cosy::ClauseInjector::UNITS;
	while (symmetry->hasClauseToInject(type)) {

            std::vector<Lit> literals = symmetry->clauseToInject(type);
            assert(literals.size() == 1);
            Lit l = literals[0];
            forbid_units.insert(l);
            if (value(l) == l_Undef)
                uncheckedEnqueue(l);

	}
    }
    max_learnts = nClauses() * learntsize_factor;
    if (max_learnts < min_learnts_lim)
        max_learnts = min_learnts_lim;
    solves++;

    max_learnts               = nClauses() * learntsize_factor;
    learntsize_adjust_confl   = learntsize_adjust_start_confl;
    learntsize_adjust_cnt     = (int)learntsize_adjust_confl;
    lbool   status            = l_Undef;

    if (verbosity >= 1){
        printf("============================[ Search Statistics ]==============================\n");
        printf("| Conflicts |          ORIGINAL         |          LEARNT          | Progress |\n");
        printf("|           |    Vars  Clauses Literals |    Limit  Clauses Lit/Cl |          |\n");
        printf("===============================================================================\n");
    }

    // Search:
    int curr_restarts = 0;
    while (status == l_Undef){
        double rest_base = luby_restart ? luby(restart_inc, curr_restarts) : pow(restart_inc, curr_restarts);
        status = search(rest_base * restart_first);
        if (!withinBudget()) break;
        curr_restarts++;
    }

    if (verbosity >= 1)
        printf("===============================================================================\n");


    // for (int i=0; i<trail_lim[0]; i++)
    //     printf("%s%d 0\n", sign(trail[i])?"-":"", var(trail[i])+1);


    if (status == l_True){
        // Extend & copy model:
        model.growTo(nVars());
        for (int i = 0; i < nVars(); i++) model[i] = value(i);
    }else if (status == l_False && conflict.size() == 0)
        ok = false;

    cancelUntil(0);
    return status;
}


bool Solver::implies(const vec<Lit>& assumps, vec<Lit>& out)
{
    trail_lim.push(trail.size());
    for (int i = 0; i < assumps.size(); i++){
        Lit a = assumps[i];

        if (value(a) == l_False){
            cancelUntil(0);
            return false;
        }else if (value(a) == l_Undef)
            uncheckedEnqueue(a);
    }

    unsigned trail_before = trail.size();
    bool     ret          = true;
    if (propagate() == CRef_Undef){
        out.clear();
        for (int j = trail_before; j < trail.size(); j++)
            out.push(trail[j]);
    }else
        ret = false;

    cancelUntil(0);
    return ret;
}

//=================================================================================================
// Writing CNF to DIMACS:
//
// FIXME: this needs to be rewritten completely.

static Var mapVar(Var x, vec<Var>& map, Var& max)
{
    if (map.size() <= x || map[x] == -1){
        map.growTo(x+1, -1);
        map[x] = max++;
    }
    return map[x];
}


void Solver::toDimacs(FILE* f, Clause& c, vec<Var>& map, Var& max)
{
    if (satisfied(c)) return;

    for (int i = 0; i < c.size(); i++)
        if (value(c[i]) != l_False)
            fprintf(f, "%s%d ", sign(c[i]) ? "-" : "", mapVar(var(c[i]), map, max)+1);
    fprintf(f, "0\n");
}


void Solver::toDimacs(const char *file, const vec<Lit>& assumps)
{
    FILE* f = fopen(file, "wr");
    if (f == NULL)
        fprintf(stderr, "could not open file %s\n", file), exit(1);
    toDimacs(f, assumps);
    fclose(f);
}


void Solver::toDimacs(FILE* f, const vec<Lit>& assumps)
{
    // Handle case when solver is in contradictory state:
    if (!ok){
        fprintf(f, "p cnf 1 2\n1 0\n-1 0\n");
        return; }

    vec<Var> map; Var max = 0;

    // Cannot use removeClauses here because it is not safe
    // to deallocate them at this point. Could be improved.
    int cnt = 0;
    for (int i = 0; i < clauses.size(); i++)
        if (!satisfied(ca[clauses[i]]))
            cnt++;

    for (int i = 0; i < clauses.size(); i++)
        if (!satisfied(ca[clauses[i]])){
            Clause& c = ca[clauses[i]];
            for (int j = 0; j < c.size(); j++)
                if (value(c[j]) != l_False)
                    mapVar(var(c[j]), map, max);
        }

    // Assumptions are added as unit clauses:
    cnt += assumps.size();

    fprintf(f, "p cnf %d %d\n", max, cnt);

    for (int i = 0; i < assumps.size(); i++){
        assert(value(assumps[i]) != l_False);
        fprintf(f, "%s%d 0\n", sign(assumps[i]) ? "-" : "", mapVar(var(assumps[i]), map, max)+1);
    }

    for (int i = 0; i < clauses.size(); i++)
        toDimacs(f, ca[clauses[i]], map, max);

    if (verbosity > 0)
        printf("Wrote DIMACS with %d variables and %d clauses.\n", max, cnt);
}


void Solver::printStats() const
{
    double cpu_time = cpuTime();
    double mem_used = memUsedPeak();
    printf("restarts              : %"PRIu64"\n", starts);
    printf("conflicts             : %-12"PRIu64"   (%.0f /sec)\n", conflicts   , conflicts   /cpu_time);
    printf("symgenconfls          : %-12" PRIu64"   (%.0f /sec)\n", symgenconfls   , symgenconfls/cpu_time);
    printf("symselconfls          : %-12" PRIu64"   (%.0f /sec)\n", symselconfls   , symselconfls/cpu_time);
    printf("decisions             : %-12"PRIu64"   (%4.2f %% random) (%.0f /sec)\n", decisions, (float)rnd_decisions*100 / (float)decisions, decisions   /cpu_time);
    printf("propagations          : %-12"PRIu64"   (%.0f /sec)\n", propagations, propagations/cpu_time);
    printf("symgenprops           : %-12" PRIu64"   (%.0f /sec)\n", symgenprops    , symgenprops /cpu_time);
    printf("symselprops           : %-12" PRIu64"   (%.0f /sec)\n", symselprops    , symselprops /cpu_time);
    printf("conflict literals     : %-12"PRIu64"   (%4.2f %% deleted)\n", tot_literals, (max_literals - tot_literals)*100 / (double)max_literals);
    if (mem_used != 0) printf("Memory used           : %.2f MB\n", mem_used);
    printf("CPU time              : %g s\n", cpu_time);
    if (symmetry)
        symmetry->printStats();
}


//=================================================================================================
// Garbage Collection methods:

void Solver::relocAll(ClauseAllocator& to)
{
    // All watchers:
    //
    watches.cleanAll();
    for (int v = 0; v < nVars(); v++)
        for (int s = 0; s < 2; s++){
            Lit p = mkLit(v, s);
            vec<Watcher>& ws = watches[p];
            for (int j = 0; j < ws.size(); j++)
                ca.reloc(ws[j].cref, to);
        }

    // All reasons:
    //
    for (int i = 0; i < trail.size(); i++){
        Var v = var(trail[i]);

        // Note: it is not safe to call 'locked()' on a relocated clause. This is why we keep
        // 'dangling' reasons here. It is safe and does not hurt.
        if (reason(v) != CRef_Undef && (ca[reason(v)].reloced() || locked(ca[reason(v)]))){
            assert(!isRemoved(reason(v)));
            ca.reloc(vardata[v].reason, to);
        }
    }

    // All learnt:
    //
    int i, j;
    for (i = j = 0; i < learnts.size(); i++)
        if (!isRemoved(learnts[i])){
            ca.reloc(learnts[i], to);
            learnts[j++] = learnts[i];
        }
    learnts.shrink(i - j);

    // All original:
    //
    for (i = j = 0; i < clauses.size(); i++)
        if (!isRemoved(clauses[i])){
            ca.reloc(clauses[i], to);
            clauses[j++] = clauses[i];
        }
    clauses.shrink(i - j);
}


void Solver::garbageCollect()
{
    // Initialize the next region to a size corresponding to the estimated utilization degree. This
    // is not precise but should avoid some unnecessary reallocations for the new region:
    ClauseAllocator to(ca.size() - ca.wasted());

    relocAll(to);
    if (verbosity >= 2)
        printf("|  Garbage collection:   %12d bytes => %12d bytes             |\n",
               ca.size()*ClauseAllocator::Unit_Size, to.size()*ClauseAllocator::Unit_Size);
    to.moveTo(ca);
}

void Solver::addGenerator(SymGenerator* g){
	generators.push(g);
}

/*
 puts true literal in front,
 if none exists, puts 2 unknown in front,
 if none exist, puts 1 unknown in front and highest false
 if none exists, puts highest false in front, and second highest false second
*/
void Solver::prepareWatches(vec<Lit>& c){
	assert(c.size()>0);
	if(value(c[0])==l_True){
		return;
	}
	for(int i=1; i<c.size(); ++i){
		if(value(c[i])==l_True){
			return; // one true lit
		}else if(value(c[i])==l_Undef){
			if(value(c[0])==l_Undef){
				Lit tmp = c[1]; c[1]=c[i]; c[i]=tmp;
				return; // two unknown lits
			}else{
				Lit tmp = c[0]; c[0]=c[i]; c[i]=c[1]; c[1]=tmp;
			}
		}else{ // value(c[i])==l_False
			if(value(c[0])==l_False && level(var(c[0]))<level(var(c[i])) ){
				Lit tmp = c[0]; c[0]=c[i]; c[i]=c[1]; c[1]=tmp;
			}else if(level(var(c[1]))<level(var(c[i])) ){
				assert(value(c[1])==l_False);
				Lit tmp = c[1]; c[1]=c[i]; c[i]=tmp;
			}
		}
	}
	// either one unknown lit or all false
}

// minimize clause through self-subsumption
// NOTE: some clauses at level 0 have no unit clause as reason, so ugly code ahead
void Solver::minimizeClause(vec<Lit>& cl){
    return;
    vec<int> minimizeTmpVec;
    vec<Lit> copyCl;
    copyCl.growTo(cl.size());
    minimizeTmpVec.growTo(cl.size());
    for(int i=0; i<cl.size(); ++i){
        copyCl[i] = cl[i];
        int vcli = var(cl[i]);
        minimizeTmpVec[i]=vcli;
        assert(seen[vcli]==0);
        seen[vcli]=1; // mark as seen
    }

    bool isSymmetry = false;
    for(int i=0; i<cl.size() && cl.size()>1; ++i) {
        if(value(cl[i])!=l_False){
            continue;
        }
        if(level(var(cl[i]))==0){
            if (forbid_units.find(~cl[i]) != forbid_units.end()) {
                isSymmetry = true;
                break;
            }
            cl.swapErase(i);
            --i;
        }else if(reason(var(cl[i]))!=CRef_Undef){
            const Clause& expl = ca[reason(var(cl[i]))];
            bool allSeen = true;
            for(int j=0; j<expl.size(); ++j){
                int var_j = var(expl[j]);
                if (forbid_units.find(~expl[j]) != forbid_units.end()) {
                    isSymmetry = true;
                    break;
                }

                if(level(var_j)!=0 && !seen[var_j]){
                    allSeen = false;
                    break;
                }
            }
            if(allSeen) {
                if (expl.symmetry()) {
                    isSymmetry = true;
                    break;
                }

                cl.swapErase(i);
                --i;
            }
        }
    }
    for(int i=0; i<minimizeTmpVec.size(); ++i){ // reset seen
        seen[minimizeTmpVec[i]]=0;
    }

    if (isSymmetry) {
        cl.clear();
        cl.growTo(copyCl.size());
        for (int i=0; i<copyCl.size() ; i++) {
            cl[i] = copyCl[i];
        }
    }
}
// void Solver::minimizeClause(vec<Lit>& cl){
//     return; // debug
// 	vec<int> minimizeTmpVec;
// 	minimizeTmpVec.growTo(cl.size());
// 	for(int i=0; i<cl.size(); ++i){
// 		int vcli = var(cl[i]);
// 		minimizeTmpVec[i]=vcli;
// 		assert(seen[vcli]==0);
// 		seen[vcli]=1; // mark as seen
// 	}
// 	for(int i=0; i<cl.size() && cl.size()>1; ++i){
// 		if(value(cl[i])!=l_False){
// 			continue;
// 		}
// 		if(level(var(cl[i]))==0){
// 			cl.swapErase(i);
// 			--i;
// 		}else if(reason(var(cl[i]))!=CRef_Undef){
// 			const Clause& expl = ca[reason(var(cl[i]))];
// 			bool allSeen = true;
// 			for(int j=0; j<expl.size(); ++j){
// 				int var_j = var(expl[j]);
// 				if(level(var_j)!=0 && !seen[var_j]){
// 					allSeen = false;
// 					break;
// 				}
// 			}
// 			if(allSeen){
// 				cl.swapErase(i);
// 				--i;
// 			}
// 		}
// 	}
// 	for(int i=0; i<minimizeTmpVec.size(); ++i){ // reset seen
// 		seen[minimizeTmpVec[i]]=0;
// 	}
// }

// NOTE: sometimes backtracks to add unit clause instead of conflict clause
CRef Solver::addClauseFromSymmetry(const Clause& original, vec<Lit>& symmetrical){
	assert(symmetrical.size()>0);

	CRef cr = ca.alloc(symmetrical, true, original.symmetry(), original.scompat());
	learnts.push(cr);
	attachClause(cr);
	claBumpActivity(ca[cr]);
	if(symmetrical.size()<=1){
            assert(false);
            cancelUntil(0);
	}else{
		cancelUntil(level(var(symmetrical[1])));
		assert(value(symmetrical[1])==l_False);
	}

	if(value(symmetrical[0])==l_Undef){
		uncheckedEnqueue(symmetrical[0], cr);
		return CRef_Undef; // unit clause, added to clause store
	}
	assert(value(symmetrical[0])==l_False);
	return cr; // conflict clause, need to backtrack
}

int Solver::addSelClause(SymGenerator* g, Lit l){
	CRef reason_l = reason(var(l));
	assert(reason_l != CRef_Undef);
	const Clause& c_l = ca[reason_l];
	for(int i=0; i<c_l.size(); ++i){
		if(value(g->getImage(c_l[i]))==l_True){ // unknown lits, keep in clause
			return 2;
		}
	}
	for(int i=0; i<c_l.size(); ++i){
		Lit symLit = g->getImage(c_l[i]);
		if(value(symLit)==l_Undef){ // unknown lits, keep in clause
			selClauses.push(symLit);
		} // else value is l_False, will never change, so can safely be ignored
	}
	int nbAddedLits = selClauses.size()-selIdx.last();
	if(nbAddedLits < 2){
		selClauses.shrink_(nbAddedLits);
		return nbAddedLits; // propagating or conflict symmetrical clause
	}

	assert(decisionLevel()>0); // NOTE: level 0 means clauses of length 1, should have been handled earlier
	assert(nbAddedLits>=2);
	int selClauseId = selProp.size(); // id for selClause
	selClauseWatches[toInt(~selClauses[selIdx.last()])]->push(selClauseId); // negation of first literal is watch
	selClauseWatches[toInt(~selClauses[selIdx.last()+1])]->push(selClauseId); // negation of second literal is watch
	selIdx.push(selClauses.size());
	selGen.push(g);
	selProp.push(var(l));

	return 3;
}

CRef Solver::learntSymmetryClause(cosy::ClauseInjector::Type type, Lit p) {
    if (symmetry != nullptr) {
        if (symmetry->hasClauseToInject(type, p)) {
            std::vector<Lit> vsbp = symmetry->clauseToInject(type, p);


            vec<Lit> sbp;
            int max_i = 0;
            int lvl = level(var(vsbp[max_i]));

            int i = 0;
            for (Lit l : vsbp) {
                sbp.push(l);
                if (i > 1 && level(var(l)) > lvl) {
                    max_i = i;
                    lvl = level(var(vsbp[max_i]));
                }
                i++;
            }
            if (max_i != 0)
                std::swap(sbp[0], sbp[max_i]);

            /*
            vec<Lit> sbp;
            for (Lit l : vsbp) {
                assert(value(l) == l_False);
                sbp.push(l);
            }
            */
            std::set<SymGenerator*>* comp = new std::set<SymGenerator*>();

            for(int i=0; i<generators.size(); ++i) {
                SymGenerator * generator = generators[i];

                if (generator->stabilize(sbp))
                    comp->insert(generator);
            }

            CRef cr = ca.alloc(sbp, true, true, comp);

            assert(ca[cr].scompat() != nullptr);
            learnts.push(cr);
            attachClause(cr);

            return cr;
        }
    }
    return CRef_Undef;
}

void Solver::notifyCNFUnits() {
    if (symmetry != nullptr) {
        for (int i=0; i<trail.size(); i++)
            symmetry->updateNotify(trail[i]);
    }
}
bool Solver::testSelClauses(){
	vec<Lit> symmetrical;
	for(int cl=0; cl<selProp.size(); ++cl){
		assert(selGen.size()==selProp.size());
		assert(cl<selProp.size());
		selGen[cl]->getSymmetricalClause(ca[reason(selProp[cl])],symmetrical);
		for(int i=0; i<symmetrical.size(); ++i){
			Lit symlit = symmetrical[i];
			seen[var(symlit)]=(sign(symlit)?2:1);
		}
		for(int i=selIdx[cl]; i<selIdx[cl+1]; ++i){
			Lit symlit = selClauses[i];
			if(seen[var(symlit)]!=(sign(symlit)?2:1)){
				printClause(symmetrical);
				for(int j=selIdx[cl]; j<selIdx[cl+1]; ++j){
					printf("%d ",toInt(selClauses[j]));
				}
				printf("-> faulty selClauseLiteral %d\n",toInt(symlit));
				return false;
			}
		}
		for(int i=0; i<symmetrical.size(); ++i){
			seen[var(symmetrical[i])]=0;
		}

		for(int i=selIdx[cl]; i<selIdx[cl+1]; ++i){
			Lit symlit = selClauses[i];
			seen[var(symlit)]=(sign(symlit)?2:1);
		}
		for(int i=0; i<symmetrical.size(); ++i){
			Lit symlit = symmetrical[i];
			if(value(symlit)!=l_False && seen[var(symlit)]!=(sign(symlit)?2:1)){
				printClause(symmetrical);
				for(int j=selIdx[cl]; j<selIdx[cl+1]; ++j){
					printf("%d ",toInt(selClauses[j]));
				}
				printf("-> missing selClauseLiteral %d\n",toInt(symlit));
				return false;
			}
		}
		for(int i=selIdx[cl]; i<selIdx[cl+1]; ++i){
			seen[var(selClauses[i])]=0;
		}
	}
	return true;
}

void Solver::initiateGenWatches(){
	genWatches.clear();
	genWatchIndices.clear();
	genWatchIndices.push(0);
	for(int v=0; v<nVars(); ++v){
		for(int g=0; g<generators.size(); ++g){
			if(generators[g]->permutes(mkLit(v))){
				genWatches.push(generators[g]);
			}
		}
		genWatchIndices.push(genWatches.size());
	}
}