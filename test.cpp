#include "FIN_MultiCurveStripping/inc/DualYieldCurveStripper.h"
#include "ATM_RuntimeBasics/inc/ATM_ThrowStrings.h"
#include "ATM_MathsFunctions/inc/Optimizer.h"
#include "ATM_MathsFunctions/inc/TaxicabOptimizer.h"
#include "ATM_MathsFunctions/inc/BroydenSolver.h"
#include "ATM_MathsFunctions/inc/ATM_Solve1D.h"

#include <algorithm>


#include "ATM_RuntimeBasics/inc/ATM_ThrowChars.h"
#include "ATM_MathsFunctions/inc/ATM_Spline_I.h"
#include "ATM_MathsFunctions/inc/ATM_Solve1D.h"
#include "ATM_MathsFunctions/inc/PowellOptimizer.h"
#include "ATM_MathsFunctions/inc/BrentMinimizer.h"
#include "ATM_MathsFunctions/inc/MOSolver.h"

#include "ATM_MathsFunctions/inc/PowellOptimizer.h"



using namespace std;
using namespace NumeriX;

namespace {
    bool InstrumentLength(const DualYieldCurveInstrument::CPtr &a, const DualYieldCurveInstrument::CPtr &b)
    {
        return a->end() < b->end();
    }

    size_t distanceBetween(const DualYieldCurveInstrument::CPtr &a, const DualYieldCurveInstrument::CPtr &b)
    {
        if (a->end() < b->end()) {
            return b->end().asExcel() - a->end().asExcel();
        }
        else
        {
            return a->end().asExcel() - b->end().asExcel();
        }
    }
}

DualYieldCurveStripper::DualYieldCurveStripper(
    const DualYieldCurveFactory::CPtr &curveFact,
    const vector<DualYieldCurveInstrument::CPtr> &instruments,
    Date nowDate,
    size_t pairingDistance)
    : curveFact_m(curveFact), nowDate_m(nowDate)
{
    pair<vector<PairingRecord>, vector<DualYieldCurveInstrument::CPtr> > pairs = makePairs(instruments, pairingDistance);
    instPairs_m = pairs.first;
    leftovers_m = pairs.second;

    if (instPairs_m.size() == 0) { throwFatalException("DualYieldCurveStripper: Must be at least one instrument pair."); }
}

pair<vector<DualYieldCurveStripper::PairingRecord>, vector<DualYieldCurveInstrument::CPtr> > 
    DualYieldCurveStripper::makePairs(const vector<DualYieldCurveInstrument::CPtr> &instruments, size_t pairingDistance)
{
    if (instruments.size() < 2) { throwFatalException("DualYieldCurveStripper: Must be at least two instruments."); }

    vector<DualYieldCurveInstrument::CPtr> sortedInstruments = instruments;
    std::sort(sortedInstruments.begin(), sortedInstruments.end(), InstrumentLength);

    // Try to pair up the instruments.
    vector<DualYieldCurveStripper::PairingRecord > ret;
    size_t firstIdx = 0;

    vector<DualYieldCurveInstrument::CPtr> unpairedInstruments;
    while (firstIdx < sortedInstruments.size()) {
        if (firstIdx == sortedInstruments.size() - 1) {
            unpairedInstruments.push_back(sortedInstruments[firstIdx]);
            firstIdx++;
            continue;
        }

        DualYieldCurveInstrument::CPtr first = sortedInstruments[firstIdx];
        DualYieldCurveInstrument::CPtr second = sortedInstruments[firstIdx+1];

        // If the instruments are close together, we can pair them.
        if (distanceBetween(first, second) <= pairingDistance) {
            pair<DualYieldCurveInstrument::CPtr, DualYieldCurveInstrument::CPtr> pairedInstruments = make_pair(first, second);
            ret.push_back(PairingRecord(pairedInstruments, unpairedInstruments));
            unpairedInstruments.clear();
            firstIdx += 2;
        }
        else
        {
            // Otherwise, we can't pair them.
            unpairedInstruments.push_back(first);
            firstIdx++;
        }
    }

    return make_pair(ret, unpairedInstruments);
}

namespace
{
    class TwoElementOptimizerObjective : public Optimizer::Objective {
    public:
        TwoElementOptimizerObjective(
            const DualYieldCurveFactory::CPtr &curveFact,
            const vector<Date> &dates,
            const vector<double> &fixedDiscountingAbs,
            const vector<double> &fixedIndexAbs,
            pair<DualYieldCurveInstrument::CPtr, DualYieldCurveInstrument::CPtr> insts
            ) : curveFact_m(curveFact), dates_m(dates), fixedDiscountingAbs_m(fixedDiscountingAbs), fixedIndexAbs_m(fixedIndexAbs), insts_m(insts)
        {}

        virtual ~TwoElementOptimizerObjective() {}

        // Gets the number of parameters that appropriate to this objective.
        virtual size_t getNumParams() const { return 2; }

        // Evaluates the objective at a point.
        virtual double at(const std::vector<double> &x) const {
            // Construct the abscissae
            vector<double> discAbs = fixedDiscountingAbs_m;
            discAbs.push_back(x[0]);
            vector<double> indexAbs = fixedIndexAbs_m;
            indexAbs.push_back(x[1]);

            // Construct the actual curves.
            DualYieldCurveFactory::CurvePair curves = curveFact_m->newCurves(dates_m, discAbs, dates_m, indexAbs);

            // Get the sum square price.
            double val1 = insts_m.first->getValueDiff(curves.discounting_m, curves.index_m);
            double val2 = insts_m.second->getValueDiff(curves.discounting_m, curves.index_m);

            return val1*val2 + val2*val2;
        }

    private:

        DualYieldCurveFactory::CPtr curveFact_m;

        vector<Date> dates_m;
        vector<double> fixedDiscountingAbs_m;
        vector<double> fixedIndexAbs_m;

        pair<DualYieldCurveInstrument::CPtr, DualYieldCurveInstrument::CPtr> insts_m;

        TwoElementOptimizerObjective(const TwoElementOptimizerObjective &);
        TwoElementOptimizerObjective &operator=(const TwoElementOptimizerObjective &);
    };


    class TwoElementSolverObjective : public MOSolver::Objective {
    public:
        TwoElementSolverObjective(
            const DualYieldCurveFactory::CPtr &curveFact,
            const vector<Date> &discountingDates,
            const vector<Date> &indexDates,
            const vector<double> &fixedDiscountingAbs,
            const vector<double> &fixedIndexAbs,
            const DualYieldCurveStripper::PairingRecord &insts
            ) : curveFact_m(curveFact), discountingDates_m(discountingDates), indexDates_m(indexDates),
            fixedDiscountingAbs_m(fixedDiscountingAbs), fixedIndexAbs_m(fixedIndexAbs), insts_m(insts)
        {}

        virtual ~TwoElementSolverObjective() {}

        /// Gets the number of input parameters appropriate to this.
        virtual size_t getNumInputParams() const { return insts_m.getNumInsts(); }

        /// Gets the number of output parameters that this will return.
        virtual size_t getNumOutputParams() const { return insts_m.getNumInsts(); }

        virtual void at(
            const std::vector<double> &x, //< The point to evaluate at.
            std::vector<double> &y  //< OUT: The evaluation.
            ) const
        {
            // Construct the abscissae
            // Discounting gets the first point.
            vector<double> discAbs = fixedDiscountingAbs_m;
            discAbs.push_back(x[0]);

            // Index gets all of the remaining points.
            vector<double> indexAbs = fixedIndexAbs_m;
            for (size_t j = 1 ; j < x.size() ; ++j) { indexAbs.push_back(x[j]); }
           
            // Construct the actual curves.
            DualYieldCurveFactory::CurvePair curves = curveFact_m->newCurves(discountingDates_m, discAbs, indexDates_m, indexAbs);

            // Get the prices.
            insts_m.evaluate(curves.discounting_m, curves.index_m, y);
        }

    private:

        DualYieldCurveFactory::CPtr curveFact_m;

        vector<Date> discountingDates_m;
        vector<double> fixedDiscountingAbs_m;
        vector<Date> indexDates_m;
        vector<double> fixedIndexAbs_m;

        DualYieldCurveStripper::PairingRecord insts_m;

        TwoElementSolverObjective(const TwoElementSolverObjective &);
        TwoElementSolverObjective &operator=(const TwoElementSolverObjective &);
    };

    class FixedDiscountingAbscissaObjective : public Function1D_I {
    public:
        typedef Shared_ptr<const FixedDiscountingAbscissaObjective> CPtr;

        FixedDiscountingAbscissaObjective(
            const DualYieldCurveInstrument::CPtr &inst,
            const DualYieldCurveFactory::CPtr &curveFact,
            const vector<Date> &discountingDates,
            const vector<double> &discountingAbscissa,
            const vector<Date> &indexDates,
            const vector<double> &fixedIndexAbscissa)
            : inst_m(inst), curveFact_m(curveFact), discountingDates_m(discountingDates),
            discountingAbscissa_m(discountingAbscissa), indexDates_m(indexDates),
            fixedIndexAbscissa_m(fixedIndexAbscissa) {}

        virtual ~FixedDiscountingAbscissaObjective() {}

        /// Evaluates for the next
        virtual double at(double nextIndexAbs) const 
        {
            // Extend the index abscissa.
            vector<double> indexAbs = fixedIndexAbscissa_m;
            indexAbs.push_back(nextIndexAbs);

            // Create some curves.
            DualYieldCurveFactory::CurvePair curves = curveFact_m->newCurves(discountingDates_m, discountingAbscissa_m, indexDates_m, indexAbs);

            return inst_m->getValueDiff(curves.discounting_m, curves.index_m);
        }

    private:

        // The instrument we are pricing.
        DualYieldCurveInstrument::CPtr inst_m;

        // The yield curve factory.
        DualYieldCurveFactory::CPtr curveFact_m;

        // The dates and abscissa for the (known) discounting curve.
        vector<Date> discountingDates_m;
        vector<double> discountingAbscissa_m;

        // The dates for the index curve, and the known part of the abscissa.
        vector<Date> indexDates_m;
        vector<double> fixedIndexAbscissa_m;

        FixedDiscountingAbscissaObjective(const FixedDiscountingAbscissaObjective &);
        FixedDiscountingAbscissaObjective &operator=(const FixedDiscountingAbscissaObjective &);
    };
}

DualYieldCurveStripper::Result DualYieldCurveStripper::strip() const {
    vector<double> discountingAbs;
    vector<double> indexAbs;
    vector<Date> discountingDates;
    vector<Date> indexDates;

            bool broydenSucceeded = false;

    for (vector<PairingRecord>::const_iterator it = instPairs_m.begin() ; 
        it != instPairs_m.end() ; ++it)
    {
        PairingRecord insts = *it;

        // The discounting curve gets a pillar for the paired instruments only.
        Date end = insts.getPairedDate();
        discountingDates.push_back(end);

        // The index curve also gets a pillar for all unpaired instruments.
        vector<Date> unpairedDates;
        insts.getUnpairedDates(unpairedDates);
        for (vector<Date>::const_iterator it = unpairedDates.begin(); it != unpairedDates.end() ; ++it) { indexDates.push_back(*it); }
        indexDates.push_back(end);

        // Get the next pair of DFs.

                        TwoElementSolverObjective *teso = new TwoElementSolverObjective(curveFact_m, discountingDates, indexDates, discountingAbs, indexAbs, insts);    
                        MOSolver::Objective::CPtr solveObj(teso);
                        Optimizer::Objective::CPtr solveObj2 = reinterpret_cast<Optimizer::Objective::CPtr&>(solveObj);
                        
        // Set the initial guess based on what the factor says a good initial guess is.
        vector<double> initialGuess(solveObj->getNumInputParams());
        initialGuess[0] = curveFact_m->discountingAbsInitialGuess();
        for (size_t j = 1 ; j < initialGuess.size() ; ++j) { initialGuess[j] = curveFact_m->indexAbsInitialGuess(); }

                        
        try 
        {

        MOSolver::TerminationCondition::CPtr termination = MOSolver::getUniformTerminationCondition(1e-9);
        MOSolver::CPtr moSolver(new BroydenSolver(100, 0.00001, termination));
        MOSolver::Result solveRes = moSolver->solve(solveObj, initialGuess);

        vector<double> solveAbs = solveRes.abscissa();

        discountingAbs.push_back(solveAbs[0]);
        for (size_t j = 1 ; j < solveAbs.size() ; ++j) { indexAbs.push_back(solveAbs[j]); }

                        // The algorithm succeeded iff the termination condition was met.
                        broydenSucceeded = termination->shouldTerminate(solveAbs);

        }
        catch ( const Exception_I&)
        {
            broydenSucceeded = false;
        }
                        
                        /***
        if (!broydenSucceeded) {
            // Failover to the more robust, but slower, Powell solver.
            Minimize1D::CPtr linMin(new BrentMinimizer());
            Optimizer::CPtr opt(new PowellOptimizer(linMin, 2000, 0.0001, 50, 50, 1e-8, Optimizer::stepSizeTerminationCondition(1e-8)));
            Optimizer::Result res = opt->findMin(solveObj2, initialGuess);

                                    vector<double> solveAbs = res.abscissa();

                                    discountingAbs.push_back(solveAbs[0]);
            for (size_t j = 1 ; j < solveAbs.size() ; ++j) { indexAbs.push_back(solveAbs[j]); }
        }
                        **/
    }

    // If we have leftover, unpaired instruments, solve each of those by adding points to the index curve.
    if (leftovers_m.size() != 0) {
        for (vector<DualYieldCurveInstrument::CPtr>::const_iterator it = leftovers_m.begin() ; it != leftovers_m.end() ; ++it) 
        {
            DualYieldCurveInstrument::CPtr inst = *it;
            indexDates.push_back(inst->end());
            FixedDiscountingAbscissaObjective::CPtr obj(new FixedDiscountingAbscissaObjective(inst, curveFact_m, discountingDates, discountingAbs, indexDates, indexAbs));

            double indexIndexAbs = 0.0;
            double tolOut = 0.0;
            int4 iterOut = 0;
            double nextIndexAbs = 0.0;
            ATM_Solve1D::solveBrent(obj, 0.0, 0.0, 1.0, 1e-9, nextIndexAbs, tolOut, iterOut, indexAbs.back(), true, 25, true, true, false);
            indexAbs.push_back(nextIndexAbs);
        }
    }

    return Result(curveFact_m->newCurves(discountingDates, discountingAbs, indexDates, indexAbs), discountingDates, discountingAbs, indexDates, indexAbs);
}

DualYieldCurveStripper::PairingRecord &DualYieldCurveStripper::PairingRecord::operator=(const DualYieldCurveStripper::PairingRecord &o)
{
    pairedInsts_m = o.pairedInsts_m;
    unpairedInsts_m = o.unpairedInsts_m;
    return *this;
}

// Gets the date of the pillar implied by the paired instruments.
Date DualYieldCurveStripper::PairingRecord::getPairedDate() const
{
    // Use the later end date.
    Date end1 = pairedInsts_m.first->end();
    Date end2 = pairedInsts_m.second->end();
    return (end1 > end2) ? end1 : end2;
}

// Gets the dates of the pillars implied by the unpaired instruments.
void DualYieldCurveStripper::PairingRecord::getUnpairedDates(std::vector<Date> &out) const
{
    out.clear();
    for (vector<DualYieldCurveInstrument::CPtr>::const_iterator it = unpairedInsts_m.begin() ; it != unpairedInsts_m.end() ; ++it)
    {
        out.push_back((*it)->end());
    }
}

void DualYieldCurveStripper::PairingRecord::evaluate(
    const YieldCurve_I::CPtr &discountingCurve,
    const YieldCurve_I::CPtr &indexCurve,
    std::vector<double> &out) const
{
    out.clear();
    out.resize(getNumInsts());
    out[0] = pairedInsts_m.first->getValueDiff(discountingCurve, indexCurve);
    out[1] = pairedInsts_m.second->getValueDiff(discountingCurve, indexCurve);

    for (size_t j = 0 ; j < unpairedInsts_m.size() ; ++j) {
        out[2+j] = unpairedInsts_m[j]->getValueDiff(discountingCurve, indexCurve);
    }
}
