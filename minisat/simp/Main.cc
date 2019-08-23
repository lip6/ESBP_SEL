/*****************************************************************************************[Main.cc]
Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
Copyright (c) 2007,      Niklas Sorensson

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

#include <errno.h>
#include <zlib.h>

#include "minisat/utils/System.h"
#include "minisat/utils/ParseUtils.h"
#include "minisat/utils/Options.h"
#include "minisat/core/Dimacs.h"
#include "minisat/simp/SimpSolver.h"
#include "minisat/core/MinisatLiteralAdapter.h"
#include "cosy/SymmetryController.h"

#include <string>

using namespace Minisat;

//=================================================================================================


static Solver* solver;
// Terminate by notifying the solver and back out gracefully. This is mainly to have a test-case
// for this feature of the Solver as it may take longer than an immediate call to '_exit()'.
static void SIGINT_interrupt(int) { solver->interrupt(); }

// Note that '_exit()' rather than 'exit()' has to be used. The reason is that 'exit()' calls
// destructors and may cause deadlocks if a malloc/free function happens to be running (these
// functions are guarded by locks for multithreaded use).
static void SIGINT_exit(int) {
    printf("\n"); printf("*** INTERRUPTED ***\n");
    if (solver->verbosity > 0){
        solver->printStats();
        printf("\n"); printf("*** INTERRUPTED ***\n"); }
    _exit(1); }


//=================================================================================================
// Main:

int main(int argc, char** argv)
{
    try {
        setUsageHelp("USAGE: %s [options] <input-file> <result-output-file>\n\n  where input may be either in plain or gzipped DIMACS.\n");
        setX86FPUPrecision();

        // Extra options:
        //
        IntOption    verb   ("MAIN", "verb",   "Verbosity level (0=silent, 1=some, 2=more).", 1, IntRange(0, 2));
        BoolOption   pre    ("MAIN", "pre",    "Completely turn on/off any preprocessing.", false);
        BoolOption   solve  ("MAIN", "solve",  "Completely turn on/off solving after preprocessing.", true);
        StringOption dimacs ("MAIN", "dimacs", "If given, stop after preprocessing and write the result to this file.");
        IntOption    cpu_lim("MAIN", "cpu-lim","Limit on CPU time allowed in seconds.\n", 0, IntRange(0, INT32_MAX));
        IntOption    mem_lim("MAIN", "mem-lim","Limit on memory usage in megabytes.\n", 0, IntRange(0, INT32_MAX));
        BoolOption   strictp("MAIN", "strict", "Validate DIMACS header during parsing.", false);
        BoolOption   linear_sym_gens   ("MAIN", "linear-sym-gens", "Use a linear number of generators for row interchangeability.", false);

        BoolOption    opt_bliss      ("SYM", "bliss",  "Parse sym file in bliss", false);
        BoolOption    opt_breakid    ("SYM", "breakid","PArse sym file in breakid format", false);

        parseOptions(argc, argv, true);

        SimpSolver  S;
        double      initial_time = cpuTime();

        if (!pre) S.eliminate(true);

        S.verbosity = verb;

        solver = &S;
        // Use signal handlers that forcibly quit until the solver will be able to respond to
        // interrupts:
        sigTerm(SIGINT_exit);



        // Try to set resource limits:
        if (cpu_lim != 0) limitTime(cpu_lim);
        if (mem_lim != 0) limitMemory(mem_lim);

        if (argc == 1)
            printf("Reading from standard input... Use '--help' for help.\n");

				std::string cnfloc = argv[1];
				gzFile in = (argc == 1) ? gzdopen(0, "rb") : gzopen(cnfloc.c_str(), "rb");
        if (in == NULL)
            printf("ERROR! Could not open file: %s\n", argc == 1 ? "<stdin>" : cnfloc.c_str()), exit(1);

				parse_DIMACS(in, S, (bool)strictp);
				gzclose(in);



        if (S.verbosity > 0){
            printf("============================[ Problem Statistics ]=============================\n");
            printf("|                                                                             |\n"); }

        FILE* res = (argc >= 3) ? fopen(argv[2], "wb") : NULL;

        if (S.verbosity > 0){
            printf("|  Number of variables:  %12d                                         |\n", S.nVars());
            printf("|  Number of clauses:    %12d                                         |\n", S.nClauses());
	          printf("|  Number of sym generators: %8d                                         |\n", S.nGenerators()); }

        double parsed_time = cpuTime();
        if (S.verbosity > 0)
            printf("|  Parse time:           %12.2f s                                       |\n", parsed_time - initial_time);

        // Change to signal-handlers that will only notify the solver and allow it to terminate
        // voluntarily:
        sigTerm(SIGINT_interrupt);

        //        S.eliminate(true);
        double simplified_time = cpuTime();
        if (S.verbosity > 0){
            printf("|  Simplification time:  %12.2f s                                       |\n", simplified_time - parsed_time);
            printf("|                                                                             |\n"); }

        if (!S.okay()){
            if (res != NULL) fprintf(res, "UNSAT\n"), fclose(res);
            if (S.verbosity > 0){
                printf("===============================================================================\n");
                printf("Solved by simplification\n");
                S.printStats();
                printf("\n"); }
            printf("UNSATISFIABLE\n");
            exit(20);
        }

        bool opt_cosy = true;

        std::string cnf_file = std::string(argv[1]);
        std::string sym_file_bliss = cnfloc + ".bliss";
        std::string symloc = cnfloc + ".sym";

        if (opt_cosy) {
            S.adapter = std::unique_ptr<cosy::LiteralAdapter<Minisat::Lit>>
                (new MinisatLiteralAdapter());

            /*std::string cnf_file = std::string(argv[1]);

            S.symmetry = std::unique_ptr<cosy::SymmetryController<Minisat::Lit>>
                (new cosy::SymmetryController<Minisat::Lit>
                 (cnf_file,
                  cosy::SymmetryFinder::Automorphism::BLISS,
                  S.adapter));
            */



            if (opt_bliss) {
                S.symmetry = std::unique_ptr<cosy::SymmetryController<Minisat::Lit>>
                    (new cosy::SymmetryController<Minisat::Lit>
                     (cnf_file,
                      sym_file_bliss, cosy::SymmetryReader::SAUCY_SYM,
                      S.adapter));
            } else if (opt_breakid) {
                S.symmetry = std::unique_ptr<cosy::SymmetryController<Minisat::Lit>>
                    (new cosy::SymmetryController<Minisat::Lit>
                     (cnf_file, symloc, cosy::SymmetryReader::BREAKID_SYM,
                      S.adapter));
            } else {
                S.symmetry = nullptr;
            }
        }


        gzFile in_sym = (argc == 1) ? gzdopen(0, "rb") : opt_breakid ? gzopen(symloc.c_str(), "rb") : gzopen(sym_file_bliss.c_str(), "rb");

        if (in_sym!=NULL){
            if (opt_breakid) {
                parse_SYMMETRY(in_sym, S, linear_sym_gens);
                gzclose(in_sym);
            } else if (opt_bliss) {
                parse_SYMMETRY_BLISS(in_sym, S);
                gzclose(in_sym);
            }

        }else{
            printf("c Did not find .sym symmetry file. Assuming no symmetry is provided.\n");
        }

        lbool ret = l_Undef;

        if (solve){
            vec<Lit> dummy;
            ret = S.solveLimited(dummy);
        }else if (S.verbosity > 0)
            printf("===============================================================================\n");

        if (dimacs && ret == l_Undef)
            S.toDimacs((const char*)dimacs);

        if (S.verbosity > 0){
            S.printStats();
            printf("\n"); }
        printf(ret == l_True ? "SATISFIABLE\n" : ret == l_False ? "UNSATISFIABLE\n" : "INDETERMINATE\n");
        res = stdout;
        if (res != NULL){
            if (ret == l_True){
                fprintf(res, "s SATISFIABLE\nv ");
                for (int i = 0; i < S.nVars(); i++)
                    if (S.model[i] != l_Undef)
                        fprintf(res, "%s%s%d", (i==0)?"":" ", (S.model[i]==l_True)?"":"-", i+1);
                fprintf(res, " 0\n");
            }else if (ret == l_False)
                fprintf(res, "s UNSATISFIABLE\n");
            else
                fprintf(res, "s INDETERMINATE\n");
            fclose(res);
        }

#ifdef NDEBUG
        exit(ret == l_True ? 10 : ret == l_False ? 20 : 0);     // (faster than "return", which will invoke the destructor for 'Solver')
#else
        return (ret == l_True ? 10 : ret == l_False ? 20 : 0);
#endif
    } catch (OutOfMemoryException&){
        printf("===============================================================================\n");
        printf("INDETERMINATE\n");
        exit(0);
    }
}
