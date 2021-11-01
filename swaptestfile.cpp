
#include "Application/inc/InstrumentIRSwap.h"
#include "Application/inc/EnumFactory.h"
#include "Application/inc/PricingResults.h"
#include "Application/inc/ModelBase.h"
#include "Application/inc/CurveTenorInterpolated.h"

#ifndef RISK_MCVAR_DISABLE
#include "AdvRisk/inc/AnalyticCapitalHelper.h"
#include "AdvRisk/inc/AnalyticCapitalInstrumentsHelper.h"
#endif

#include "MOD_Exposed/inc/Underlying.h"
#include "INT_Exposed/inc/Payoff_I.h"
#include "INT_Exposed/inc/NXKernel_I.h"

#include "FIN_Basics/inc/FIN_EventSchedule.h"

#include "ATM_MathsFunctions/inc/ATM_MinorFunctions.h"


namespace {

TokenType::DontInitialize dont_initialize;

const TokenType tkIRSwap(dont_initialize);
const TokenType tkCCIRSwap(dont_initialize);

const TokenType tkFixedNotional(dont_initialize);
const TokenType tkFloatNotional(dont_initialize);

const TokenType tkFixedCurrency(dont_initialize);
const TokenType tkFloatCurrency(dont_initialize);

const TokenType tkFloatStubIndexCurve(dont_initialize);

const TokenType tkFixedRollDate(dont_initialize);
const TokenType tkFixedRollDateType(dont_initialize);
const TokenType tkFixedFrontStubType(dont_initialize);
const TokenType tkFixedBackStubType(dont_initialize);

const TokenType tkFloatRollDate(dont_initialize);
const TokenType tkFloatRollDateType(dont_initialize);
const TokenType tkFloatFrontStubType(dont_initialize);
const TokenType tkFloatBackStubType(dont_initialize);

const TokenType tkFixedCompoundingType(dont_initialize);

const TokenType tkRateEndOfMonth(dont_initialize);

const StringVector &getInterfaces()
{
    static StringVector interfaces_s;
    if (interfaces_s.empty())
    {
        interfaces_s.push_back("MATURITY");
    }
    return interfaces_s;
}
const StringVector interfaces_s = getInterfaces();

const string empty_string_s;
const string none_s("NONE");
const string zero_bd_s("0BD");
const string swap_str("SWAP");
const string coupon_leg_str("COUPONLEG");
const string coupon_legStart_str("COUPONLEGSTART");
const string coupon_legEnd_str("COUPONLEGEND");
const string fund_legStart_str("FUNDLEGSTART");
const string fund_legEnd_str("FUNDLEGEND");
const string fund_leg_str("FUNDINGLEG");
const string coupon_log_str("COUPONLOG");
const string fund_log_str("FUNDLOG");
const string coupon_date_str("COUPONDATES");
const string fund_date_str("FUNDDATES");
const string fund_date_dcf_str("FUNDDATESDCF");
const string coupon_dcf_str("COUPONDATESDCF");
const string fixed_str("FIXED");
const string fixed_cashflow_str("FIXEDCASHFLOW");
const string float_str("FLOATRATE");
const string libor_str("LIBOR");
const string fixed_notional("FIXEDNOTIONAL");
const string float_notional("FLOATNOTIONAL");
const string fund_front_stub_date_str("FUNDFRONTSTUBDATE");
const string fund_back_stub_date_str("FUNDBACKSTUBDATE");
const string libor_front_short_stub_str("LIBORFRONTSHORTSTUB");
const string libor_front_long_stub_str("LIBORFRONTLONGSTUB");
const string libor_back_short_stub_str("LIBORBACKSHORTSTUB");
const string libor_back_long_stub_str("LIBORBACKLONGSTUB");

const char* irswap_s("IRSWAP::");
const char* ccirswap_s("IRCCSWAP::");

vector<FIN_PaymentStream::OutFlow> noFlows_s;

}

bool
InstrumentIRSwap::staticInit()
{
    static bool init = false;
    if (!init)
    {
        tkIRSwap.init("SWAP");
        tkCCIRSwap.init("CC SWAP");

        tkFixedNotional.init("FIXED NOTIONAL");
        tkFloatNotional.init("FLOAT NOTIONAL");

        tkFixedCurrency.init("FIXED CURRENCY");
        tkFloatCurrency.init("FLOAT CURRENCY");

        tkFloatStubIndexCurve.init("FLOAT STUB INDEX CURVE");

        tkFixedRollDate.init("FIXED ROLL DATE");
        tkFixedRollDateType.init("FIXED ROLL DATE TYPE");
        tkFixedFrontStubType.init("FIXED FRONT STUB TYPE");
        tkFixedBackStubType.init("FIXED BACK STUB TYPE");

        tkFloatRollDate.init("FLOAT ROLL DATE");
        tkFloatRollDateType.init("FLOAT ROLL DATE TYPE");
        tkFloatFrontStubType.init("FLOAT FRONT STUB TYPE");
        tkFloatBackStubType.init("FLOAT BACK STUB TYPE");

        tkFixedCompoundingType.init("FIXED COMPOUNDING TYPE");

        tkRateEndOfMonth.init("RATE END OF MONTH");

        init = true;
    }
    return init;
}

namespace {

// ======================================================================
// Utility classes for helping with schedules.
// ======================================================================

// TODO: need EOM
struct LegCheck
{
    bool basis_required_m;
    TokenType tk_basis, tk_basis_inconv;
    TokenType tk_freq, tk_freq_inconv;
    TokenType tk_acc_cal, tk_acc_conv, tk_acc_cal_inconv, tk_acc_conv_inconv;
    TokenType tk_pay_cal, tk_pay_conv, tk_pay_cal_inconv, tk_pay_conv_inconv;
    TokenType tk_pay_lag, tk_pay_lag_inconv;
    TokenType tk_front_stub, tk_long_stub, tk_front_stub_inconv, tk_long_stub_inconv;

    void addChecks(ParseCheck& check, ConventionSpec& spec, bool includeStubFlags = true) const
    {
        if (basis_required_m)
            check.addRequired(spec, tk_basis, ParseString, tk_basis_inconv);
        else
            check.addOptional(spec, tk_basis, ParseString, tk_basis_inconv);

        check.addRequired(spec, tk_freq, ParseString, tk_freq_inconv);
        check.addOptional(spec, tk_acc_cal, ParseString, tk_acc_cal_inconv);
        check.addOptional(spec, tk_acc_conv, ParseString, tk_acc_conv_inconv);
        check.addOptional(spec, tk_pay_cal, ParseString, tk_pay_cal_inconv);
        check.addOptional(spec, tk_pay_conv, ParseString, tk_pay_conv_inconv);
        check.addOptional(spec, tk_pay_lag, ParseString, tk_pay_lag_inconv);
        if (includeStubFlags)
        {
        check.addOptional(spec, tk_front_stub, ParseBool, tk_front_stub_inconv);
        check.addOptional(spec, tk_long_stub, ParseBool, tk_long_stub_inconv);
    }
    }

protected:
    LegCheck()
    {
        basis_required_m = true;
        tk_basis_inconv = tkCommonBasis;
        tk_freq_inconv = tkCommonFrequency;
        tk_acc_cal_inconv = tkEventsAccrualCalendar;
        tk_acc_conv_inconv = tkEventsAccrualConvention;
        tk_pay_cal_inconv = tkEventsPayCalendar;
        tk_pay_conv_inconv = tkEventsPayConvention;
        tk_pay_lag_inconv = tkEventsPayLag;
        tk_front_stub_inconv = tkEventsFrontStub;
        tk_long_stub_inconv = tkEventsLongStub;
    }
};

// TODO: Fixed leg needs compounding type
struct FixedLegCheck : public LegCheck
{
    FixedLegCheck()
    {
        tk_basis = tkCommonBasis;
        tk_freq = tkCurveFixedFrequency;
        tk_acc_cal = tkEventsFixedAccrualCalendar;
        tk_acc_conv = tkEventsFixedAccrualConvention;
        tk_pay_cal = tkEventsFixedPayCalendar;
        tk_pay_conv = tkEventsFixedPayConvention;
        tk_pay_lag = tkEventsFixedPayLag;
        tk_front_stub = tkEventsFixedFrontStub;
        tk_long_stub = tkEventsFixedLongStub;
    }
};

// TODO: Float leg needs FIXING LAG/CALENDAR/CONVENTION
struct FloatLegCheck : public LegCheck
{
    FloatLegCheck()
    {
        basis_required_m = false;
        tk_basis = tkFloatBasis;
        tk_freq = tkCurveFloatingFrequency;
        tk_acc_cal = tkEventsFloatAccrualCalendar;
        tk_acc_conv = tkEventsFloatAccrualConvention;
        tk_pay_cal = tkEventsFloatPayCalendar;
        tk_pay_conv = tkEventsFloatPayConvention;
        tk_pay_lag = tkEventsFloatPayLag;
        tk_front_stub = tkEventsFloatFrontStub;
        tk_long_stub = tkEventsFloatLongStub;
    }
};

struct LegCal
{
    Tenor freq_m;
    Tenor pay_lag_m;
    string acc_cal_name_m, acc_conv_name_m;
    Calendar_I::Ptr acc_cal_m;
    Calendar_I::Ptr pay_cal_m;
    bool front_stub_m, long_stub_m;
    BasisType basisType_m;

    void init(
        const ParseResult& result,
        const LegCheck& leg,
        LegCal* defaults)
    {
        defaults_m = defaults;

        {
            ApplicationVariant basisStr = result.queryVariant(leg.tk_basis);
            basisType_m = basisStr.isEmpty()
                ? BasisUNKNOWN
                : EnumFactory::convertBasisType(basisStr.asString());
            if (basisType_m == BasisUNKNOWN && defaults)
                basisType_m = defaults->basisType_m;
        }

        freq_m = result.getTenor(leg.tk_freq);

        acc_cal_name_m = result.getString(leg.tk_acc_cal, none_s);
        const string pay_cal = result.getString(leg.tk_pay_cal, acc_cal_name_m);

        acc_conv_name_m = result.getString(leg.tk_acc_conv, none_s);
        const string pay_conv = result.getString(leg.tk_pay_conv, acc_conv_name_m);

        acc_cal_m = result.getCalendar(acc_conv_name_m, acc_cal_name_m);
        pay_cal_m = result.getCalendar(pay_conv, pay_cal);

        pay_lag_m = result.getTenor(leg.tk_pay_lag, zero_bd_s);

        front_stub_m = result.getBool(leg.tk_front_stub, true);
        long_stub_m = result.getBool(leg.tk_long_stub, true);
    }

    const Basis_I::CPtr& getBasis(
        const vector<Date>* accrualDates,
        const Date* endDate,
        const bool* eom) const
    {
        if (!basis_m) {
            if (basisType_m == BasisUNKNOWN && defaults_m)
                basis_m = defaults_m->getBasis(accrualDates, endDate, eom);
            else {
                basis_m = InstrumentBase::getBasis(
                    basisType_m, acc_cal_m, accrualDates,
                    &freq_m, endDate, eom);
            }
        }

        return basis_m;
    }

private:
    mutable Basis_I::CPtr basis_m;
    LegCal* defaults_m;
};

struct ConvSpec : public ConventionSpec
{
    ConvSpec(const char* swapType, TokenType tk) :
    swapType_m(swapType), ConventionSpec(tk) {}

    virtual string getComputedConventionID(const ParseResult& result) const
    {
        ParseResult::Ptr p = result.querySingle(tkSwapType);
        if (!p || !p->isString())
            return empty_string_s;

        string swap_type = p->getString();
        if (swap_type.empty())
            return empty_string_s;

        return StringFunctions::fixName(swapType_m + swap_type);
    }

    const char* swapType_m;
};

struct FixedLegConvSpec : public ConvSpec
{
    FixedLegConvSpec(const char* swapType) : ConvSpec(swapType, tkFixedLegConvention) {}
    string getComputedConventionID(const ParseResult& result) const
    {
        string id = ConvSpec::getComputedConventionID(result);
        return id.empty() ? id : id+"::FIXEDLEG";
    }
};

struct FloatLegConvSpec : public ConvSpec
{
    FloatLegConvSpec(const char* swapType) : ConvSpec(swapType, tkFloatLegConvention) {}
    string getComputedConventionID(const ParseResult& result) const
    {
        string id = ConvSpec::getComputedConventionID(result);
        return id.empty() ? id : id+"::FLOATLEG";
    }
};

struct FloatRateConvSpec : public ConvSpec
{
    FloatRateConvSpec(const char* swapType) : ConvSpec(swapType, tkFloatRateConvention) {}
    string getComputedConventionID(const ParseResult& result) const
    {
        string id = ConvSpec::getComputedConventionID(result);
        return id.empty() ? id : id+"::FLOATRATE";
    }
};

// ======================================================================
// Swap objects
// ======================================================================

class Swap : public InstrumentIRSwap
{
public:
    struct Check : public ParseCheck
    {
        Check(const char* swapType, const TokenType& ttype, const char* msg, int priority) :
            ParseCheck(InstrumentBase::getParser(), ttype, msg, priority),
            conv_spec_m(swapType, tkCommonConvention)
        {
            // These inputs cannot come from conventions.
            addOptional(tkSwapType, ParseString);

            addOptional(tkIndexFixing, ParseID);

            // These inputs can come from conventions, and use the same header
            setDefaultConventionSpec(&conv_spec_m);

            addOptional(tkCurvePriority, ParseDouble);
            addOptional(tkCurveInterval, ParseString);

            addOptional(tkFloatIndexCurve, ParseID);
        }

        ConvSpec conv_spec_m;
    };

    Date getFixedEndDate(const Date& nowDate) const
    {
        return calc_schedule(nowDate, false)->fixed_m.dates_m.back()->getPeriodEndDate();
    }

    Date getFloatEndDate(const Date& nowDate) const
    {
        return calc_schedule(nowDate, false)->float_m.dates_m.back()->getPeriodEndDate();
    }

    Date getFixedStartDate(const Date& nowDate) const
    {
        return calc_schedule(nowDate, false)->fixed_m.dates_m.front()->getPeriodStartDate();
    }

    Date getFloatStartDate(const Date& nowDate) const
    {
        return calc_schedule(nowDate, false)->float_m.dates_m.front()->getPeriodStartDate();
    }

    virtual void getStartAndEndDatesForFixedFloatSwap(
        const Date& nowDate,
        Date& start,
        Date& end) const
    {
        Date fixedStart = getFixedStartDate(nowDate);
        Date floatStart = getFloatStartDate(nowDate);
        start = (fixedStart < floatStart) ? fixedStart : floatStart;
        if (start < nowDate)
            start = nowDate;

        Date fixedEnd = getFixedEndDate(nowDate);
        Date floatEnd = getFloatEndDate(nowDate);
        end = (fixedEnd > floatEnd) ? fixedEnd : floatEnd;
        if (end < nowDate)
            end = nowDate;
    }

    virtual const Currency& getFixedCurrency() const
    {
        return getCurrency();
    }

    virtual const Currency& getFloatCurrency() const
    {
        return getCurrency();
    }

    const Currency& getCurrency1() const
    {
        return getFixedCurrency();
    }

    const Currency& getCurrency2() const
    {
        return getFloatCurrency();
    }

    virtual CurveYieldBase::Ptr getFloatIndexCurve() const
    {
        return floatIndexCurveBinder_m.query<CurveYieldBase>();
    }

    virtual CurveTenorInterpolated::Ptr getFloatStubIndexCurve() const
    {
        return floatStubIndexCurve_m;
    }

    virtual const Currency& getPayoutCurrency() const
    {
        return getCurrency() != Currency::Failed ? getCurrency() : getFixedCurrency();
    }

    virtual CurveYieldBase::Ptr getFxProjectionCurve() const
    {
        return NullPtr;
    }

    virtual CurveYieldBase::Ptr getFxProjectionPayoutCurve() const
    {
        return NullPtr;
    }

    virtual vector<Date> getFixedFxFixingDates() const
    {
        return vector<Date>();
    }

    virtual vector<Date> getFloatFxFixingDates() const
    {
        return vector<Date>();
    }

    virtual pair<Date,Date> getFixedNotionalFxFixingDates() const
    {
        return pair<Date,Date>();
    }

    virtual pair<Date,Date> getFloatNotionalFxFixingDates() const
    {
        return pair<Date,Date>();
    }

    virtual bool isNonDeliverable() const
    {
        return false;
    }

    virtual DateFunction_I::CPtr getFxFixings() const
    {
        return NullPtr;
    }

    virtual pair<bool,bool> getNotionalExchange() const
    {
        return pair<bool,bool>(true, true);
    }

    virtual bool initialize()
    {
        return true;
    }

    virtual bool validate()
    {
        const ParseResult& result = getParameters();

        // By default we say this is the one-leg version.
        // Override this in subclass if that is not correct.
        hasTwoLegs_m = false;

        nominal_cal_m = getTrivialCalendar();

        priority_m = result.getDouble(tkCurvePriority, 1);
        interval_m = result.getTenor(tkCurveInterval, zero_bd_s);

        floatIndexCurveBinder_m = MarketBinder(tkFloatIndexCurve,result);

        return true;
    }

    virtual void process()
    {
    }

    virtual void view(ApplicationData& out) const {}

    void viewWithSchedule(ApplicationData& out, const ScheduleInfo::Ptr &schedule) const
    {
        const FixedScheduleInfo& fixed = schedule->fixed_m;
        for(size_t i = 0; i < fixed.dates_m.size(); i++)
        {
            const EventSchedule_I::IPeriod::CPtr& period = fixed.dates_m[i];
            out.appendData(tkEventsAccrualStart, period->getPeriodStartDate());
            out.appendData(tkEventsAccrualEnd, period->getPeriodEndDate());
            out.appendData(tkEventsPaymentDate, period->getPaymentDate());
        }

        if(hasTwoLegs_m)
        {
            const FloatScheduleInfo& floating = schedule->float_m;

            if(!floating.dates_m.size())
                return;

            for(size_t i = 0; i < floating.dates_m.size(); ++i)
            {
                const EventSchedule_I::IPeriod::CPtr& period = floating.dates_m[i];

                out.appendData(tkEventsFloatLegFixDate, period->getFixingDate());
                out.appendData(tkEventsFloatLegAccrualStart, period->getPeriodStartDate());
                out.appendData(tkEventsFloatLegAccrualEnd, period->getPeriodEndDate());
                out.appendData(tkEventsFloatLegPayDate, period->getPaymentDate());
            }
        }
    }

    struct Priceable
    {
        virtual ~Priceable() {}

        virtual Shared_ptr<FIN_SwapInstrument::PriceResults> price(
            const Date& nowDate,
            const Date& valuationDate,
            double domesticScale,
            const YieldCurve_I::CPtr& domesticCurve,
            const YieldCurve_I::CPtr& domesticIndexCurve,
            const FIN_TenorInterpolatedCurve::CPtr& domesticStubIndexCurve,
            double foreignScale,
            const YieldCurve_I::CPtr& foreignCurve,
            const YieldCurve_I::CPtr& foreignIndexCurve,
            const FIN_TenorInterpolatedCurve::CPtr& foreignStubIndexCurve,
            bool includeValueDate,
            bool computeFlows,
            vector<FIN_PaymentStream::OutFlow>& flowsOut,
            vector<FIN_PaymentStream::OutFlow>& fixsOut,
            vector<FIN_PaymentStream::OutFlow>& floatsOut) const = 0;

        virtual const string& description() const = 0;
    };

    class SwapDualYieldCurveInstrument : public DualYieldCurveInstrument {
    public:

        SwapDualYieldCurveInstrument(const FIN_SwapInstrument::CPtr &swapInst,
            const std::string& id,
            const std::string& name,
            const Date& nowDate,
            const Date& start,
            const Date& end,
            double priority,
            const Tenor& interval)
            : DualYieldCurveInstrument(id, name, nowDate, start, end, priority, interval), swapInst_m(swapInst) {}

        virtual ~SwapDualYieldCurveInstrument() {}

        virtual double getValueDiff(
            ValueDiffPolicy policy,
            const YieldCurve_I::CPtr &discountingCurve,
            const YieldCurve_I::CPtr &indexCurve) const 
        {
            vector<FIN_PaymentStream::OutFlow> flowsOut;
            vector<FIN_PaymentStream::OutFlow> fixsOut;
            vector<FIN_PaymentStream::OutFlow> floatsOut;
            Shared_ptr<FIN_SwapInstrument::PriceResults> prices = 
                swapInst_m->price(getNowDate(), getNowDate(), 1.0, 
                discountingCurve, indexCurve, 1.0, discountingCurve, indexCurve, false, false, flowsOut, fixsOut, floatsOut);
            string msg;
            return prices->getPrice(&msg);

        }

    private:

        FIN_SwapInstrument::CPtr swapInst_m;

        SwapDualYieldCurveInstrument(const SwapDualYieldCurveInstrument &);
        SwapDualYieldCurveInstrument &operator=(const SwapDualYieldCurveInstrument &);
    };

    // Gets a Dual Yield Curve Product for this instrument.
    virtual DualYieldCurveInstrument::CPtr getDualYieldCurveProduct(
        const Date& nowDate,
        const IQuotes::CPtr& quotes,
        const Bump::CPtr& bump,
        int& bumpUsedOut,
        ApplicationWarning& warning) const 
    {
        Date startOut;
        Date endOut;
        FIN_SwapInstrument::CPtr swapInst = this->instrument(nowDate, quotes, 
            bump.asInstanceOf<const BumpShift>().get(), true, CurveTenorInterpolated::Ptr(), 
            bumpUsedOut, warning, startOut, endOut, false);
        return DualYieldCurveInstrument::CPtr(new SwapDualYieldCurveInstrument(swapInst, getID().toString(), "", nowDate, swapInst->start(), swapInst->end(), priority_m, interval_m));
    }

    /// A yield curve product where some of the yield curves are held constant
    /// during pricing.  This is the abstract base class for all of our swap
    /// stripping products.
    class CurriedSwapProduct : public YCProductInstrument
    {
    public:
        virtual ~CurriedSwapProduct() {}
        CurriedSwapProduct(
            const ObjectID & id,
            const Date& nowDate,
            const Date& spotDate,
            const Shared_ptr<const Priceable>& swap,
            double rate,
            const Date& start,
            const Date& end,
            bool fixedIsReporting,
            double fixedScale,
            double floatScale,
            double priority,
            const Tenor& interval) :
        YCProductInstrument(id.toString(), nowDate, swap->description(), rate, start, end, priority, interval),
        swap_m(swap),
        spotDate_m(spotDate),
        fixedIsReporting_m(fixedIsReporting),
        fixedScale_m(fixedScale),
        floatScale_m(floatScale)
        {
        }

        virtual double getValueDiff(const YieldCurve_I& curve) const
        {
            Shared_ptr<const YieldCurve_I> yc(&curve, RefCountBase::NoRefCountTag());

            YieldCurve_I::CPtr fix, floatDisc, floatProj;
            getCurves(yc, fix, floatDisc, floatProj);

            double fixedScale = fixedScale_m;
            double floatScale = floatScale_m;

            if (spotDate_m != getNowDate())
            {
                if (fixedIsReporting_m)
                    floatScale *= fix->getDF(getNowDate(), spotDate_m)/floatDisc->getDF(getNowDate(), spotDate_m);
                else
                    fixedScale *= floatDisc->getDF(getNowDate(), spotDate_m)/fix->getDF(getNowDate(), spotDate_m);
            }

            Shared_ptr<FIN_SwapInstrument::PriceResults> swapPrices =
                swap_m->price(getNowDate(),
                              getNowDate(),
                              fixedScale,
                              fix,
                              fix,
                              FIN_TenorInterpolatedCurve::CPtr(),
                              floatScale,
                              floatDisc,
                              floatProj,
                              FIN_TenorInterpolatedCurve::CPtr(),
                              true,
                              false,
                              noFlows_s, noFlows_s, noFlows_s);

            return swapPrices->getPrice(NULL);
        }

        virtual void getCurves(
            const YieldCurve_I::CPtr& inYC,
            YieldCurve_I::CPtr& fix,
            YieldCurve_I::CPtr& floatDisc,
            YieldCurve_I::CPtr& floatProj
            ) const = 0;

    private:
        Shared_ptr<const Priceable> swap_m;
        Date spotDate_m;
        bool fixedIsReporting_m;
        double fixedScale_m;
        double floatScale_m;
    };

    // An abstract product factory.
    struct YCProductFactory
    {
        virtual ~YCProductFactory() {}
        virtual YCProductInstrument* get(
            const string& id,
            const Date& nowDate,
            const Date& spotDate,
            const Shared_ptr<const Priceable>& swap,
            double rate,
            const Date& start,
            const Date& end,
            double fixedScale,
            double floatScale,
            double priority,
            const Tenor& interval) const = 0;
    };

    struct SinglePriceable : public Priceable
    {
        SinglePriceable(const FIN_SwapInstrument::CPtr& swap) :
            swap_m(swap)
        {}

        virtual Shared_ptr<FIN_SwapInstrument::PriceResults> price(
            const Date& nowDate,
            const Date& valuationDate,
            double domesticScale,
            const YieldCurve_I::CPtr& domesticCurve,
            const YieldCurve_I::CPtr& domesticIndexCurve,
            const FIN_TenorInterpolatedCurve::CPtr& domesticStubIndexCurve,
            double foreignScale,
            const YieldCurve_I::CPtr& foreignCurve,
            const YieldCurve_I::CPtr& foreignIndexCurve,
            const FIN_TenorInterpolatedCurve::CPtr& foreignStubIndexCurve,
            bool includeValueDate,
            bool computeFlows,
            vector<FIN_PaymentStream::OutFlow>& flowsOut,
            vector<FIN_PaymentStream::OutFlow>& fixsOut,
            vector<FIN_PaymentStream::OutFlow>& floatsOut) const
        {
            return swap_m->price(nowDate,
                                 valuationDate,
                                 domesticScale,
                                 domesticCurve,
                                 domesticIndexCurve,
                                 domesticStubIndexCurve,
                                 foreignScale,
                                 foreignCurve,
                                 foreignIndexCurve,
                                 foreignStubIndexCurve,
                                 includeValueDate,
                                 computeFlows,
                                 flowsOut, fixsOut, floatsOut);
        }

        virtual const string& description() const
        {
            return swap_m->description();
        }

    private:
        FIN_SwapInstrument::CPtr swap_m;
    };

    struct CCPriceable : public Priceable
    {
        CCPriceable(const CrossCurrencyBasisSwapInstrument::CPtr& swap) :
            swap_m(swap)
        {}

        virtual Shared_ptr<FIN_SwapInstrument::PriceResults> price(
            const Date& nowDate,
            const Date& valuationDate,
            double domesticScale,
            const YieldCurve_I::CPtr& domesticCurve,
            const YieldCurve_I::CPtr& domesticIndexCurve,
            const FIN_TenorInterpolatedCurve::CPtr& domesticStubIndexCurve,
            double foreignScale,
            const YieldCurve_I::CPtr& foreignCurve,
            const YieldCurve_I::CPtr& foreignIndexCurve,
            const FIN_TenorInterpolatedCurve::CPtr& foreignStubIndexCurve,
            bool includeValueDate,
            bool computeFlows,
            vector<FIN_PaymentStream::OutFlow>& flowsOut,
            vector<FIN_PaymentStream::OutFlow>& fixsOut,
            vector<FIN_PaymentStream::OutFlow>& floatsOut) const
        {
            vector<FIN_PaymentStream::OutFlow> resetsOut;
            const bool isBasisSwap = true;
            return swap_m->price(nowDate,
                                 valuationDate,
                                 domesticScale,
                                 domesticCurve,
                                 domesticIndexCurve,
                                 domesticStubIndexCurve,
                                 foreignScale,
                                 foreignCurve,
                                 foreignIndexCurve,
                                 foreignStubIndexCurve,
                                 ConvexityAdjustmentModelParams(),
                                 includeValueDate,
                                 isBasisSwap,
                                 computeFlows,
                                 flowsOut, fixsOut, floatsOut, resetsOut);
        }

        virtual const string& description() const
        {
            return swap_m->description();
        }

    private:
        CrossCurrencyBasisSwapInstrument::CPtr swap_m;
   };

    /// A yield curve product factory where the floating-leg curves
    /// are held constant while the fixed discounting curve is varied.
    struct FixDiscYCFactory : public YCProductFactory
    {
        FixDiscYCFactory(
            const YieldCurve_I::CPtr& floatDisc,
            const YieldCurve_I::CPtr& floatProj) :
        floatDisc_m(floatDisc), floatProj_m(floatProj)
        {
            if (!floatDisc_m)
                throwFatalException("Float discounting curve must be non-null.");
        }

        class Product : public CurriedSwapProduct
        {
        public:
            Product(
                const ObjectID & id,
                const Date& nowDate,
                const Date& spotDate,
                const Shared_ptr<const Priceable>& swap,
                double rate,
                const Date& start,
                const Date& end,
                double fixedScale,
                double floatScale,
                double priority,
                const Tenor& interval,
                YieldCurve_I::CPtr floatDisc,
                YieldCurve_I::CPtr floatProj) :
            CurriedSwapProduct(
                id.toString(), nowDate, spotDate, swap, rate,
                start, end, true, fixedScale, floatScale,
                priority, interval),
            floatDisc_m(floatDisc), floatProj_m(floatProj)
            {
            }

            virtual void getCurves(
                const YieldCurve_I::CPtr& inYC,
                YieldCurve_I::CPtr& fix,
                YieldCurve_I::CPtr& floatDisc,
                YieldCurve_I::CPtr& floatProj
                ) const
            {
                fix = inYC;
                floatDisc = floatDisc_m;
                floatProj = floatProj_m? floatProj_m : floatDisc_m;
            }

        private:
            YieldCurve_I::CPtr floatDisc_m;
            YieldCurve_I::CPtr floatProj_m;
        };

        virtual YCProductInstrument* get(
            const string& id,
            const Date& nowDate,
            const Date& spotDate,
            const Shared_ptr<const Priceable>& swap,
            double rate,
            const Date& start,
            const Date& end,
            double fixedScale,
            double floatScale,
            double priority,
            const Tenor& interval) const
        {
            return new Product(
                id, nowDate, spotDate, swap, rate, start, end,
                fixedScale, floatScale, priority, interval, floatDisc_m, floatProj_m);
        }

    private:
        YieldCurve_I::CPtr floatDisc_m;
        YieldCurve_I::CPtr floatProj_m;
    };

    /// A yield curve product factory where the fixed-leg discounting
    /// curve is held constant while the floating-leg discounting curve is varied.
    /// If no floating projection curve is specified, that curve is treated as
    /// being equal to the float-leg discounting curve.
    struct FloatDiscYCFactory : public YCProductFactory
    {
        FloatDiscYCFactory(
            const YieldCurve_I::CPtr& fixDisc,
            const YieldCurve_I::CPtr& floatProj) :
        fixDisc_m(fixDisc), floatProj_m(floatProj)
        {
            if (!fixDisc_m)
                throwFatalException("Fixed discounting curve must be non-null.");
        }

        class Product : public CurriedSwapProduct
        {
        public:
            Product(
                const ObjectID & id,
                const Date& nowDate,
                const Date& spotDate,
                const Shared_ptr<const Priceable>& swap,
                double rate,
                const Date& start,
                const Date& end,
                double fixedScale,
                double floatScale,
                double priority,
                const Tenor& interval,
                const YieldCurve_I::CPtr& fixDisc,
                const YieldCurve_I::CPtr& floatProj) :
            CurriedSwapProduct(
                id.toString(), nowDate, spotDate, swap, rate,
                start, end, false, fixedScale, floatScale,
                priority, interval),
            fixDisc_m(fixDisc), floatProj_m(floatProj)
            {
            }

            virtual void getCurves(
                const YieldCurve_I::CPtr& inYC,
                YieldCurve_I::CPtr& fix,
                YieldCurve_I::CPtr& floatDisc,
                YieldCurve_I::CPtr& floatProj
                ) const
            {
                fix = fixDisc_m;
                floatDisc = inYC;
                floatProj = floatProj_m? floatProj_m : inYC;
            }

        private:
            YieldCurve_I::CPtr fixDisc_m;
            YieldCurve_I::CPtr floatProj_m;
        };

        virtual YCProductInstrument* get(
            const string& id,
            const Date& nowDate,
            const Date& spotDate,
            const Shared_ptr<const Priceable>& swap,
            double rate,
            const Date& start,
            const Date& end,
            double fixedScale,
            double floatScale,
            double priority,
            const Tenor& interval) const
        {
            return new Product(
                id, nowDate, spotDate, swap, rate, start, end,
                fixedScale, floatScale, priority, interval, fixDisc_m, floatProj_m);
        }

    private:
        YieldCurve_I::CPtr fixDisc_m;
        YieldCurve_I::CPtr floatProj_m;
    };

    /// A yield curve product factory where no curves are held; all are set to
    /// the one that is being stripped.  Use for simple yield curve stripping.
    struct SimpleYCFactory : public YCProductFactory
    {
        SimpleYCFactory()
        {
        }

        class Product : public CurriedSwapProduct
        {
        public:
            Product(
                const ObjectID & id,
                const Date& nowDate,
                const Date& spotDate,
                const Shared_ptr<const Priceable>& swap,
                double rate,
                const Date& start,
                const Date& end,
                double fixedScale,
                double floatScale,
                double priority,
                const Tenor& interval) :
            CurriedSwapProduct(
                id.toString(), nowDate, spotDate, swap, rate,
                start, end, true, fixedScale, floatScale,
                priority, interval)
            {
            }

            virtual void getCurves(
                const YieldCurve_I::CPtr& inYC,
                YieldCurve_I::CPtr& fix,
                YieldCurve_I::CPtr& floatDisc,
                YieldCurve_I::CPtr& floatProj
                ) const
            {
                fix = inYC;
                floatDisc = inYC;
                floatProj = inYC;
            }
        };

        virtual YCProductInstrument* get(
            const string& id,
            const Date& nowDate,
            const Date& spotDate,
            const Shared_ptr<const Priceable>& swap,
            double rate,
            const Date& start,
            const Date& end,
            double fixedScale,
            double floatScale,
            double priority,
            const Tenor& interval) const
        {
            return new Product(
                id, nowDate, spotDate, swap, rate, start, end,
                fixedScale, floatScale, priority, interval);
        }
    };

    /// A yield curve product factory where both discounting curves are
    /// held constant, while the floating-leg projection curve is varied.
    struct FloatProjYCFactory : public YCProductFactory
    {
        FloatProjYCFactory(
            const YieldCurve_I::CPtr& fixDisc,
            const YieldCurve_I::CPtr& floatDisc,
            bool fixedIsReporting) :
        fixDisc_m(fixDisc), floatDisc_m(floatDisc), fixedIsReporting_m(fixedIsReporting)
        {
            if (!fixDisc_m)
                throwFatalException("Fixed discounting curve must be non-null.");
            if (!floatDisc_m)
                throwFatalException("Floating discounting curve must be non-null.");
        }

        class Product : public CurriedSwapProduct
        {
        public:
            Product(
                const ObjectID & id,
                const Date& nowDate,
                const Date& spotDate,
                const Shared_ptr<const Priceable>& swap,
                double rate,
                const Date& start,
                const Date& end,
                bool fixedIsReporting,
                double fixedScale,
                double floatScale,
                double priority,
                const Tenor& interval,
                const YieldCurve_I::CPtr& fixDisc,
                const YieldCurve_I::CPtr& floatDisc) :
            CurriedSwapProduct(
                id.toString(), nowDate, spotDate, swap, rate,
                start, end, fixedIsReporting, fixedScale, floatScale,
                priority, interval),
            fixDisc_m(fixDisc), floatDisc_m(floatDisc)
            {
            }

            virtual void getCurves(
                const YieldCurve_I::CPtr& inYC,
                YieldCurve_I::CPtr& fix,
                YieldCurve_I::CPtr& floatDisc,
                YieldCurve_I::CPtr& floatProj
                ) const
            {
                fix = fixDisc_m;
                floatDisc = floatDisc_m;
                floatProj = inYC;
            }

        private:
            YieldCurve_I::CPtr fixDisc_m;
            YieldCurve_I::CPtr floatDisc_m;
        };

        virtual YCProductInstrument* get(
            const string& id,
            const Date& nowDate,
            const Date& spotDate,
            const Shared_ptr<const Priceable>& swap,
            double rate,
            const Date& start,
            const Date& end,
            double fixedScale,
            double floatScale,
            double priority,
            const Tenor& interval) const
        {
            return new Product(
                id, nowDate, spotDate, swap, rate, start, end, fixedIsReporting_m,
                fixedScale, floatScale, priority, interval, fixDisc_m, floatDisc_m);
        }

    private:
        YieldCurve_I::CPtr fixDisc_m;
        YieldCurve_I::CPtr floatDisc_m;
        bool fixedIsReporting_m;
    };

    virtual FIN_SwapInstrument::Ptr instrument(const Date& now_date,
                                               const IQuotes::CPtr quotes,
                                               const BumpShift* bump,
                                               bool unitNotional,
                                               const CurveTenorInterpolated::Ptr& floatStubIndexCurve,
                                               const Currency& curveCurrency,
                                               int& bump_used_out,
                                               ApplicationWarning& warn,
                                               Date& start_out,
                                               Date& end_out,
                                               bool fudge_first_fixing) const = 0;

    // We keep this function around solely for the sake of CurveYield::getBVP(),
    // which in turn exists only for the risk reports.  DO NOT use for curve
    // striping or pricing.
    IInstrumentDescriptor::Ptr instrument(const Date& nowDate,
                                          const IQuotes::CPtr quotes,
                                          ApplicationWarning& warn,
                                          bool fudgeFirstFixing) const
    {
        int ignore = 0;
        Date start, end;
        return instrument(nowDate,
                          quotes,
                          NULL,
                          false,
                          floatStubIndexCurve_m,
                          getCurrency(),
                          ignore, warn,
                          start, end,
                          fudgeFirstFixing);
    }

    virtual CrossCurrencyBasisSwapInstrument::CPtr ccInstrument(const Date& now_date,
                                                                const pair<bool,bool>& notionalExchange,
                                                                const IQuotes::CPtr quotes,
                                                                const BumpShift* bump,
                                                                bool unitNotional,
                                                                const CurveTenorInterpolated::Ptr& floatStubIndexCurve,
                                                                const Currency& curveCurrency,
                                                                int& bump_used_out,
                                                                ApplicationWarning& warn,
                                                                Date& start_out,
                                                                Date& end_out,
                                                                bool fudge_first_fixing) const
    {
        FIN_SwapInstrument::Ptr swap = instrument(now_date,
                                                  quotes, bump,
                                                  unitNotional,
                                                  floatStubIndexCurve,
                                                  curveCurrency,
                                                  bump_used_out,
                                                  warn,
                                                  start_out, end_out,
                                                  fudge_first_fixing);
        NotionalExchange notExObj = NotionalExchange(notionalExchange.first, notionalExchange.second, false);
        return mkSharedPtr(new CrossCurrencyBasisSwapInstrument(swap,
                                                                notExObj));
    }

    virtual FIN_SwapInstrument::Ptr instrument(const Date& now_date,
                                               const IQuotes::CPtr quotes,
                                               const BumpShift* bump,
                                               bool unitNotional,
                                               const CurveTenorInterpolated::Ptr& floatStubIndexCurve,
                                               int& bump_used_out,
                                               ApplicationWarning& warn,
                                               Date& start_out,
                                               Date& end_out,
                                               bool fudge_first_fixing) const
    {
        return instrument(now_date,
                          quotes, bump,
                          unitNotional,
                          floatStubIndexCurve,
                          getCurrency(),
                          bump_used_out,
                          warn,
                          start_out, end_out,
                          fudge_first_fixing);
    }

    virtual EventSchedule_I* constructEvents(const Date& now_date, ApplicationWarning&)
    {
        AutoPtr<EventSchedule_I> events(EventSchedule_I::createEventSchedule(now_date));

        ScheduleInfo::Ptr sched = calc_schedule(now_date, false);

        if(sched)
        {
            events->addCashflow(coupon_date_str, sched->fixed_m.dates_m);
            if(hasTwoLegs_m)
                events->addCashflow(fund_date_str, sched->float_m.dates_m);
        }

        events->addCashflow(coupon_legStart_str, sched->fixed_m.dates_m.front()->getPeriodStartDate());
        events->addCashflow(coupon_legEnd_str, sched->fixed_m.dates_m.back()->getPaymentDate());

        events->addCashflow(fund_legStart_str, sched->float_m.dates_m.front()->getPeriodStartDate());
        events->addCashflow(fund_legEnd_str, sched->float_m.dates_m.back()->getPaymentDate());

        if(sched->float_m.dates_m.front()->isStubPeriod())
            events->addCashflow(fund_front_stub_date_str, sched->float_m.dates_m.front());

        if(sched->float_m.dates_m.size() != 1 && sched->float_m.dates_m.back()->isStubPeriod())
            events->addCashflow(fund_back_stub_date_str, sched->float_m.dates_m.back());

        return events.release();
    }

    virtual void priceNames(KernelPriceable_I::PriceNames &pn) const
    {
        pn.clear();
        pn.priceNames_m.push_back(coupon_leg_str);

        if(hasTwoLegs_m)
        {
            pn.priceNames_m.push_back(swap_str);
            pn.priceNames_m.push_back(fund_leg_str);
        }

        pn.arrayNames_m.clear();

        pn.logNames_m.push_back(coupon_log_str);

        if(hasTwoLegs_m)
            pn.logNames_m.push_back(fund_log_str);
    }

    virtual void getArrayValues(const string& name,
                                NXKernel_I* kernel,
                                ResultType resultType,
                                vector<double>& valsOut)
    {
        valsOut.clear();
    }

    virtual double getArrayValue(const string& name,
                                 NXKernel_I* kernel,
                                 ResultType resultType,
                                 int loc)
    {
        return 0;
    }

    virtual PricingResultType getPricingResultType(const string &header) const
    {
        using namespace StringFunctions;
        if(hasTwoLegs_m)
        {
            if(equal(header,swap_str)) return prNPV;
        }
        else
        {
            if(equal(header,coupon_leg_str)) return prNPV;
        }

        return prCommonFailed;
    }

protected:
    class SwapPayoff : public Payoff_I
    {
    public:
        SwapPayoff(const CurrencyType fixedCurrType,
                   const CurrencyType floatCurrType,
                   bool doFrontStubInterpolation,
                   bool doBackStubInterpolation,
                   const pair<double,double>& frontStubRateWeights,
                   const pair<double,double>& backStubRateWeights) :
        isCrossCurrency_m(fixedCurrType != floatCurrType),
        fixedCurrType_m(fixedCurrType),
        floatCurrType_m(floatCurrType),
        doFrontStubInterpolation_m(doFrontStubInterpolation),
        doBackStubInterpolation_m(doBackStubInterpolation),
        frontStubRateWeights_m(frontStubRateWeights),
        backStubRateWeights_m(backStubRateWeights)
        {}

        virtual const PricingDirectionType getPricingDirection() const
        {
            return DirectionEither;
        }

    protected:
        void doFixedLeg(const Underlying& notional,
                        Underlying& fixedLeg)
        {
            const Underlying dcf = get(coupon_dcf_str);
            const Underlying fixedRate = get(fixed_str);
            const Underlying coupon = dcf * fixedRate * notional * (isCrossCurrency_m ? -1.0 : 1.0);
            logPayment(coupon, coupon_date_str, EffectiveDateThisPay, coupon_log_str, fixedCurrType_m);
            fixedLeg += cash(coupon, coupon_date_str,EffectiveDateThisPay, fixedCurrType_m);
        }

        void doFixedLeg(Underlying& fixedLeg)
        {
            const Underlying fixedCashflow = get(fixed_cashflow_str);
            const Underlying coupon = fixedCashflow * (isCrossCurrency_m ? -1.0 : 1.0);
            logPayment(coupon, coupon_date_str, EffectiveDateThisPay, coupon_log_str, fixedCurrType_m);
            fixedLeg += cash(coupon, coupon_date_str,EffectiveDateThisPay, fixedCurrType_m);
        }

        void addFloatLegCoupon(const Underlying& liborRate,
                               const Underlying& spread,
                               const Underlying& dcf,
                               const Underlying& notional,
                               const string& dateString,
                               const string& logString,
                               Underlying& floatLeg)
        {
            Underlying coupon = (liborRate + spread) * dcf * notional;
            logPayment(coupon, dateString, EffectiveDateThisPay, logString, floatCurrType_m);
            floatLeg += cash(coupon, dateString, EffectiveDateThisPay, floatCurrType_m);
        }

        void doFloatLeg(const Underlying& notional,
                        Underlying& floatLeg)
        {
            if(doFrontStubInterpolation_m && isActive(fund_front_stub_date_str))
            {
                Underlying liborRate = frontStubRateWeights_m.first*get(libor_front_short_stub_str);
                if(frontStubRateWeights_m.second != 0.0)
                    liborRate += frontStubRateWeights_m.second*get(libor_front_long_stub_str);

                addFloatLegCoupon(liborRate,
                                  get(float_str),
                                  get(fund_date_dcf_str),
                                  notional,
                                  fund_front_stub_date_str,
                                  fund_log_str,
                                  floatLeg);
            }            
            else if(doBackStubInterpolation_m && isActive(fund_back_stub_date_str))
            {
                Underlying liborRate = backStubRateWeights_m.first*get(libor_back_short_stub_str);
                if(backStubRateWeights_m.second != 0.0)
                    liborRate += backStubRateWeights_m.second*get(libor_back_long_stub_str);

                addFloatLegCoupon(liborRate,
                                  get(float_str),
                                  get(fund_date_dcf_str),
                                  notional,
                                  fund_back_stub_date_str,
                                  fund_log_str,
                                  floatLeg);
            }
            else
            {
                addFloatLegCoupon(get(libor_str),
                                  get(float_str),
                                  get(fund_date_dcf_str),
                                  notional,
                                  fund_date_str,
                                  fund_log_str,
                                  floatLeg);
            }
        }

        void doNotionalExchange(const Underlying& fixedNotional,
                                const Underlying& floatNotional,
                                bool initialNotionalExchange,
                                bool finalNotionalExchange,
                                Underlying& fixedLeg,
                                Underlying& floatLeg)
        {
            if(initialNotionalExchange && isActive(coupon_legStart_str))
            {
                logPayment(fixedNotional, coupon_legStart_str, EffectiveDateThisPay, coupon_log_str, fixedCurrType_m);
                fixedLeg += cash(fixedNotional, coupon_legStart_str, EffectiveDateThisPay, fixedCurrType_m);
            }
            if(finalNotionalExchange && isActive(coupon_legEnd_str))
            {
                logPayment(-fixedNotional, coupon_legEnd_str, EffectiveDateThisPay, coupon_log_str, fixedCurrType_m);
                fixedLeg += cash(-fixedNotional,  coupon_legEnd_str,EffectiveDateThisPay, fixedCurrType_m);
            }
            if(initialNotionalExchange && isActive(fund_legStart_str))
            {
                logPayment(-floatNotional, fund_legStart_str, EffectiveDateThisPay, fund_log_str, floatCurrType_m);
                floatLeg += cash(-floatNotional, fund_legStart_str, EffectiveDateThisPay, floatCurrType_m);
            }
            if(finalNotionalExchange && isActive(fund_legEnd_str))
            {
                logPayment(floatNotional, fund_legEnd_str, EffectiveDateThisPay, fund_log_str, floatCurrType_m);
                floatLeg += cash(floatNotional, fund_legEnd_str, EffectiveDateThisPay, floatCurrType_m);
            }
        }

        void getSwap(const Underlying& fixedLeg,
                     const Underlying& floatLeg,
                     Underlying& swap)
        {
            // fixed leg is negated for cross currency swaps.
            swap = isCrossCurrency_m ? floatLeg + fixedLeg : floatLeg - fixedLeg;
        }

    protected:
        CurrencyType fixedCurrType_m;
        CurrencyType floatCurrType_m;

        bool doFrontStubInterpolation_m;
        bool doBackStubInterpolation_m;
        pair<double,double> frontStubRateWeights_m;
        pair<double,double> backStubRateWeights_m;

        bool isCrossCurrency_m;
    };

    virtual ScheduleInfo::Ptr calc_schedule(const Date& now_date,
                                            bool fudge_first_fixing) const
    {
        throwAppException("Calculation of schedule from now date not implemented");
        return NullPtr;
    }

    virtual void registerData(const NXKernel_I::Ptr& kernel,
                              const Bump* bump,
                              int& bumpUsedOut) = 0;

    virtual void registerPayoff(const NXKernel_I::Ptr& kernel,
                                CurrencyType fixedCurrencyType,
                                CurrencyType floatCurrencyType,
                                bool doFrontStubInterpolation,
                                bool doBackStubInterpolation,
                                const pair<double,double>& frontStubRateWeights,
                                const pair<double,double>& backStubRateWeights) = 0;

    bool doFrontStubInterpolation(const ScheduleInfo::Ptr& schedule,
                                  const CurveTenorInterpolated::Ptr& floatStubIndexCurve) const
    {
        if (!schedule || !floatStubIndexCurve)
            return false;

        const vector<EventSchedule_I::IPeriod::CPtr>& floatDates = schedule->float_m.dates_m;
        if( floatDates.front()->isStubPeriod() && floatStubIndexCurve )
            return true;

        return false;
    }

    bool doBackStubInterpolation(const ScheduleInfo::Ptr& schedule,
                                 const CurveTenorInterpolated::Ptr& floatStubIndexCurve) const
    {
        if (!schedule || !floatStubIndexCurve)
            return false;

        const vector<EventSchedule_I::IPeriod::CPtr>& floatDates = schedule->float_m.dates_m;
        if( (floatDates.size() > 1) && floatDates.back()->isStubPeriod() && floatStubIndexCurve )
            return true;

        return false;
    }

    void setStubWeightsAndRateEndDates(const ScheduleInfo::Ptr& schedule,
                                       const CurveTenorInterpolated::Ptr& floatStubIndexCurve,
                                       pair<Date,Date>& frontStubRateEndDates,
                                       pair<double,double>& frontStubRateWeights,
                                       pair<Date,Date>& backStubRateEndDates,
                                       pair<double,double>& backStubRateWeights) const
    {
        Date stubStartDate;
        Date stubEndDate;
        const vector<EventSchedule_I::IPeriod::CPtr>& floatDates = schedule->float_m.dates_m;

        if(floatStubIndexCurve && floatDates.front()->isStubPeriod())
        {
            stubStartDate = floatDates.front()->getPeriodStartDate();
            stubEndDate = floatDates.front()->getPeriodEndDate();

            Calendar_I::CPtr cal = schedule->float_m.acc_cal_m;
            Basis_I::CPtr rateBasis = schedule->float_m.rateBasis_m;
            floatStubIndexCurve->getStubWeightsAndRateEndDates(stubStartDate,
                                                               stubEndDate,
                                                               rateBasis,
                                                               cal,
                                                               frontStubRateEndDates,
                                                               frontStubRateWeights);
        }

        if(floatDates.size() != 1 && floatDates.back()->isStubPeriod())
        {
            stubStartDate = floatDates.back()->getPeriodStartDate();
            stubEndDate = floatDates.back()->getPeriodEndDate();

            Calendar_I::CPtr cal = schedule->float_m.acc_cal_m;
            Basis_I::CPtr rateBasis = schedule->float_m.rateBasis_m;
            floatStubIndexCurve->getStubWeightsAndRateEndDates(stubStartDate,
                                                               stubEndDate,
                                                               rateBasis,
                                                               cal,
                                                               backStubRateEndDates,
                                                               backStubRateWeights);
        }
    }

    void doBaseRegistration(const NXKernel_I::Ptr& kernel,
                            Model_I::PtrCRef model,
                            const Bump* bump,
                            ApplicationWarning& warning,
                            int& bumpUsedOut,
                            CurrencyType& fixedCurrencyType,
                            CurrencyType& floatCurrencyType)
    {
        fixedCurrencyType = Domestic;
        floatCurrencyType = getFixedCurrency() == getFloatCurrency() ? Domestic : Foreign1;
        CurrencyType payoutCurrencyType = Domestic;

        if(model)
        {
            fixedCurrencyType = getCurrencyType(*model, getFixedCurrency(), warning);
            floatCurrencyType = getCurrencyType(*model, getFloatCurrency(), warning);
            payoutCurrencyType = getCurrencyType(*model, getPayoutCurrency(), warning);

            if(warning.isFatal())
                return;
        }

        kernel->registerProduct(coupon_leg_str);
        kernel->registerProduct(fund_leg_str);
        kernel->registerProduct(swap_str);

        registerData(kernel,
                     bump, bumpUsedOut);

		ScheduleInfo::Ptr schedule;
        if(model->isNowDateSet()) { schedule = calc_schedule(model->nowDate(), false); }

        pair<double,double> frontStubRateWeights;
        pair<double,double> backStubRateWeights;
        if(schedule)
        {
            if(doFrontStubInterpolation(schedule, floatStubIndexCurve_m) || doBackStubInterpolation(schedule, floatStubIndexCurve_m))
            {
                pair<Date,Date> frontStubRateEndDates;
                pair<Date,Date> backStubRateEndDates;
                setStubWeightsAndRateEndDates(schedule,
                                              floatStubIndexCurve_m,
                                              frontStubRateEndDates,
                                              frontStubRateWeights,
                                              backStubRateEndDates,
                                              backStubRateWeights);


            }
        }

        registerPayoff(kernel,
                       fixedCurrencyType,
                       floatCurrencyType,
                       doFrontStubInterpolation(schedule, floatStubIndexCurve_m),
                       doBackStubInterpolation(schedule, floatStubIndexCurve_m),
                       frontStubRateWeights,
                       backStubRateWeights);
    }

    void queryBumps(BumpQueryContainer& result,
                    ApplicationWarning& warning) const
    {
        if(CurveYieldBase::Ptr floatIndexCurve = getFloatIndexCurve())
            floatIndexCurve->queryBumps(result, warning);

        if(floatStubIndexCurve_m)
            floatStubIndexCurve_m->queryBumps(result, warning);
    }

    void registerStubIndex(const Date& stubStartDate,
                           const Date& stubEndDate,
                           const NXKernel_I::Ptr& kernel,
                           string fund_stub_date_str,
                           string libor_short_stub_str,
                           string libor_long_stub_str,
                           const CurrencyType& floatCurrencyType,
                           const Bump* bump,
                           ApplicationWarning& warning,
                           int& bumpUsedOut,
                           BasisType rateBasis,
                           const Tenor* rateFreq,
                           const Calendar_I* rateAccCal,
                           const Tenor* rateFixingLag,
                           const Calendar_I* rateFixingCalendar)
    {
        pair<FIN_TenorCurveData::CPtr, FIN_TenorCurveData::CPtr> curveData;
        pair<FIN_TenorFixingsData::CPtr, FIN_TenorFixingsData::CPtr> fixingsData;
        pair<double, double> stubWeights;
        pair<Date, Date> stubRateEndDates;

        Basis_I::CPtr swapRateBasis;
        swapRateBasis = Basis_I::createBasis(rateBasis);

        Calendar_I::CPtr swapRateAccCal;
        swapRateAccCal = Shared_ptr<const Calendar_I>(rateAccCal, RefCountBase::NoRefCountTag());

        Bump::CPtr b(bump, RefCountBase::NoRefCountTag());
        YieldCurve_I::Ptr floatStubIndexCurve = floatStubIndexCurve_m->bumpYieldCurve(b, bumpUsedOut, warning);
        
        FIN_TenorInterpolatedCurve::CPtr stubIndexCurve = floatStubIndexCurve.asInstanceOf<FIN_TenorInterpolatedCurve>();
        if(!stubIndexCurve)
        {
            warning.throwFatal("Stub index curve does not exist");
        }

        stubIndexCurve->getStubCurvesAndWeights(stubStartDate,
                                                stubEndDate,
                                                swapRateBasis,
                                                swapRateAccCal,
                                                curveData,
                                                stubWeights,
                                                stubRateEndDates);

        stubIndexCurve->getStubFixingsAndWeights(stubStartDate,
                                                 stubEndDate,
                                                 swapRateBasis,
                                                 swapRateAccCal,
                                                 fixingsData,
                                                 stubWeights);

        Tenor shortRateTenor =
            curveData.first->getRateConventions()->getTenor() == Tenor::Empty ?
            *rateFreq : curveData.first->getRateConventions()->getTenor();
        kernel->registerLIBOR(libor_short_stub_str,
                              TenorLIBOR,
                              floatCurrencyType,
                              fund_stub_date_str,
                              shortRateTenor,
                              *rateAccCal,
                              *rateFixingLag,
                              *rateFixingCalendar,
                              rateBasis,
                              curveData.first->getCurve());

        Tenor longRateTenor =
            curveData.second->getRateConventions()->getTenor() == Tenor::Empty ?
            *rateFreq : curveData.second->getRateConventions()->getTenor();
        kernel->registerLIBOR(libor_long_stub_str,
                              TenorLIBOR,
                              floatCurrencyType,
                              fund_stub_date_str,
                              longRateTenor,
                              *rateAccCal,
                              *rateFixingLag,
                              *rateFixingCalendar,
                              rateBasis,
                              curveData.second->getCurve());

        if(fixingsData.first->getFixings())
            if ( DateFunction_I::CPtr fp = fixingsData.first->getFixings() )
                kernel->registerFixings(libor_short_stub_str, *fp);

        if(fixingsData.second->getFixings())
            if ( DateFunction_I::CPtr fp = fixingsData.second->getFixings() )
                kernel->registerFixings(libor_long_stub_str, *fp);

    }

    void doRegistrationTwoLegs(const NXKernel_I::Ptr& kernel,
                               Model_I::PtrCRef model,
                               const Bump* bump,
                               ApplicationWarning& warning,
                               int& bumpUsedOut,
                               BasisType fixedBasisType,
                               BasisType floatBasisType,
                               BasisType rateBasis,
                               const Tenor* rateFreq,
                               const Calendar_I* rateAccCal,
                               const Tenor* rateFixingLag,
                               const Calendar_I* rateFixingCalendar)
    {
        CurrencyType fixedCurrencyType;
        CurrencyType floatCurrencyType;
        doBaseRegistration(kernel,
                           model,
                           bump,
                           warning,
                           bumpUsedOut,
                           fixedCurrencyType,
                           floatCurrencyType);

        kernel->registerDCF(coupon_dcf_str, coupon_date_str, fixedBasisType);
        kernel->registerDCF(fund_date_dcf_str, fund_date_str, floatBasisType);

        YieldCurve_I::Ptr floatIndexCurve;
        if (CurveYieldBase::Ptr fp = getFloatIndexCurve())
        {
            Bump::CPtr b(bump, RefCountBase::NoRefCountTag());
            floatIndexCurve = fp->bumpYieldCurve(b, bumpUsedOut, warning);
        }

        if(rateFreq)
        {
            kernel->registerLIBOR(libor_str,
                                  TenorLIBOR,
                                  floatCurrencyType,
                                  fund_date_str,
                                  *rateFreq,
                                  *rateAccCal,
                                  *rateFixingLag,
                                  *rateFixingCalendar,
                                  rateBasis,
                                  floatIndexCurve);
        }
        else
        {
            kernel->registerLIBOR(libor_str,
                                  AccrualLIBOR,
                                  floatCurrencyType,
                                  fund_date_str,
                                  rateBasis,
                                  floatIndexCurve);
        }

        if(floatStubIndexCurve_m)
        {
            Bump::CPtr b(bump, RefCountBase::NoRefCountTag());
            YieldCurve_I::Ptr floatStubIndexCurve = floatStubIndexCurve_m->bumpYieldCurve(b, bumpUsedOut, warning);

            const FIN_TenorInterpolatedCurve* stubIndexCurve =
                dynamic_cast<const FIN_TenorInterpolatedCurve*>(floatStubIndexCurve.get());
            if(!stubIndexCurve)
            {
                warning.throwFatal("Stub index curve does not exist");
            }

            pair<FIN_TenorCurveData::CPtr, FIN_TenorCurveData::CPtr> curveData;
            pair<FIN_TenorFixingsData::CPtr, FIN_TenorFixingsData::CPtr> fixingsData;
            pair<double, double> stubWeights;
            pair<Date, Date> stubRateEndDates;

    		ScheduleInfo::Ptr schedule;
            if(model->isNowDateSet())   // TODO: This is used elsewhere, but is a problem as many models don't support this
            {
                schedule = calc_schedule(model->nowDate(), false);
            }

            if (schedule)
            {
                Date frontStubStartDate;
                Date frontStubEndDate;

                if (schedule->float_m.dates_m.front()->isStubPeriod())
                {
                    frontStubStartDate = schedule->float_m.dates_m.front()->getPeriodStartDate();
                    frontStubEndDate = schedule->float_m.dates_m.front()->getPeriodEndDate();

                    registerStubIndex(frontStubStartDate,
                                      frontStubEndDate,
                                      kernel,
                                      fund_front_stub_date_str,
                                      libor_front_short_stub_str,
                                      libor_front_long_stub_str,
                                      floatCurrencyType,
                                      bump,
                                      warning,
                                      bumpUsedOut,
                                      rateBasis,
                                      rateFreq,
                                      rateAccCal,
                                      rateFixingLag,
                                      rateFixingCalendar);
                }
                
                if (schedule->float_m.dates_m.size() > 1 && schedule->float_m.dates_m.back()->isStubPeriod())
                {
                    Date backStubStartDate = schedule->float_m.dates_m.back()->getPeriodStartDate();
                    Date backStubEndDate = schedule->float_m.dates_m.back()->getPeriodEndDate();

                    registerStubIndex(backStubStartDate,
                                      backStubEndDate,
                                      kernel,
                                      fund_back_stub_date_str,
                                      libor_back_short_stub_str,
                                      libor_back_long_stub_str,
                                      floatCurrencyType,
                                      bump,
                                      warning,
                                      bumpUsedOut,
                                      rateBasis,
                                      rateFreq,
                                      rateAccCal,
                                      rateFixingLag,
                                      rateFixingCalendar);
                }
            }
        }

        if( DateFunction_I::CPtr fp = queryFixingsFunc() )
            kernel->registerFixings(libor_str, *fp);
    }

    void parseCompoundingFrequency(const ParseResult& result,
                                   const TokenType& token)
    {
        const string ct = result.getString(token, "NONE");
        compounding_frequency_m = EnumFactory::getCompoundingFrequency(ct).getOrElse(0);
    }

protected:
    bool hasTwoLegs_m;
    Calendar_I::CPtr nominal_cal_m;

    MarketBinder floatIndexCurveBinder_m;
    CurveTenorInterpolated::Ptr floatStubIndexCurve_m;

    double priority_m;
    Tenor interval_m;
    size_t compounding_frequency_m;
};

class ConstantParameterSwap : public Swap
{
public:
    struct Check : public Swap::Check
    {
        Check(const char* swapType, const TokenType& ttype, const char* msg, int priority) :
            Swap::Check(swapType, ttype, msg, priority)
        {
        }
    };

    virtual bool constantNotional() const
    {
        return true;
    }

    virtual double getNotional() const
    {
        return fixedNotional_m; // Only good for single currency
    }

    virtual bool hasYCProducts() const
    {
        return true;
    }

    virtual void getQuotes(QuoteSet& quotesOut) const
    {
        fixedRate_m->getQuotes(quotesOut);
        floatRate_m->getQuotes(quotesOut);
    }

    virtual InstrumentQuote::NameCRef rateQuote()  const
    {
        return fixedRate_m->getName();
    }

    virtual InstrumentQuote::NameCRef priceQuote() const
    {
        return empty_string_s;
    }

    virtual InstrumentQuote::NameCRef volQuote()   const
    {
        return empty_string_s;
    }

    virtual double getRate(const IQuotes::CPtr quotes,
                           const BumpShift* bump,
                           int& bumpUsedOut,
                           ApplicationWarning& warning) const
    {
        return fixedRate_m->getValue(quotes, bump, bumpUsedOut);
    }

    virtual double getPrice(const IQuotes::CPtr quotes,
                            const BumpShift* bump,
                            int& bumpUsedOut,
                            ApplicationWarning& warning) const
    {
        return 0;
    }

    virtual double getVol(const IQuotes::CPtr quotes,
                          const BumpShift* bump,
                          int& bumpUsedOut,
                          ApplicationWarning& warning) const
    {
        return 0;
    }

    // Uses a product factory to create a stripping product.  Various
    // specific methods below instantiate a factory and then call
    // this method.
    virtual YCProductInstrument::Ptr getAbstractProduct(bool isCC,
                                                        const YCProductFactory& factory,
                                                        const Date& nowDate,
                                                        const Date& spotDate,
                                                        double fixedScale,
                                                        double floatScale,
                                                        const Currency& curveCurrency,
                                                        const IQuotes::CPtr& quotes,
                                                        const Bump::CPtr& bump,
                                                        int& bumpUsedOut,
                                                        bool fudgeFirstFixing,
                                                        ApplicationWarning& warning) const
    {
        if(priority_m == 999)
            return YCProductInstrument::Ptr();

        Date start, end;

        const BumpShift* bumpShift = dynamic_cast<const BumpShift*>(bump.get());

        Shared_ptr<const Priceable> priceable;
        if(isCC) {
            CrossCurrencyBasisSwapInstrument::CPtr inst = ccInstrument(
                nowDate,
                pair<bool,bool>(true,true),
                quotes,
                bumpShift,
                true,
                floatStubIndexCurve_m,
                curveCurrency,
                bumpUsedOut,
                warning,
                start,
                end,
                fudgeFirstFixing);
            priceable = new CCPriceable(inst);
        }
        else {
            FIN_SwapInstrument::Ptr inst = instrument(
                nowDate,
                quotes,
                bumpShift,
                true,
                floatStubIndexCurve_m,
                curveCurrency,
                bumpUsedOut,
                warning,
                start,
                end,
                fudgeFirstFixing);
            priceable = new SinglePriceable(inst);
        }

        if(warning.isFatal())
            return YCProductInstrument::Ptr();

        return YCProductInstrument::Ptr(factory.get(getID().toString(),
                                                    nowDate, spotDate,
                                                    priceable,
                                                    fixedRate_m->getValue(quotes, bumpShift, bumpUsedOut),
                                                    start, end,
                                                    fixedScale, floatScale,
                                                    priority_m, interval_m));
    }

    virtual YCProductInstrument::Ptr getDiscYCProduct(const Currency& domCurrency,
                                                      const Currency& forCurrency,
                                                      const YieldCurve_I::CPtr& forDisc,
                                                      const YieldCurve_I::CPtr& forProj,
                                                      const YieldCurve_I::CPtr& domProj,
                                                      const Date& nowDate,
                                                      const Date& spotDate,
                                                      double fixedScale,
                                                      double floatScale,
                                                      const Currency& curveCurrency,
                                                      const IQuotes::CPtr& quotes,
                                                      const Bump::CPtr& bump,
                                                      int& bumpUsedOut,
                                                      bool fudgeFirstFixing,
                                                      ApplicationWarning& warning,
                                                      IInstrumentStripCC::YCUsed& ycUsed) const
    {
        Shared_ptr<YCProductFactory> factory;

        if(getFixedCurrency() == curveCurrency)
        {
            factory = new FixDiscYCFactory(forDisc, forProj);
        }
        else
        {
            factory = new FloatDiscYCFactory(forDisc, domProj);
        }

        if(getFixedCurrency() == domCurrency)
        {
            // We did not use the domestic projection curve.
            ycUsed.forDisc_m = true;
            ycUsed.forProj_m = true;
            ycUsed.domDisc_m = true;
        }
        else
        {
            // We did not use the foreign projection curve.
            ycUsed.forDisc_m = true;
            ycUsed.domDisc_m = true;
            ycUsed.domProj_m = true;
        }

        return YCProductInstrument::Ptr(getAbstractProduct(true,
                                                           *factory,
                                                           nowDate, spotDate,
                                                           fixedScale, floatScale,
                                                           curveCurrency,
                                                           quotes, bump, bumpUsedOut,
                                                           fudgeFirstFixing,
                                                           warning));
    }

    // Use this to strip projection curves for the floating leg.
    virtual YCProductInstrument::Ptr getProjYCProduct(const YieldCurve_I::CPtr& domDisc,
                                                      const Currency& domCurrency,
                                                      const YieldCurve_I::CPtr& forDisc,
                                                      const Currency& forCurrency,
                                                      const Date& nowDate,
                                                      const Date& spotDate,
                                                      double fixedScale,
                                                      double floatScale,
                                                      const Currency& curveCurrency,
                                                      const IQuotes::CPtr& quotes,
                                                      const Bump::CPtr& bump,
                                                      int& bumpUsedOut,
                                                      bool fudgeFirstFixing,
                                                      ApplicationWarning& warning,
                                                      IInstrumentStripCC::YCUsed &ycUsed) const
    {
        Shared_ptr<YCProductFactory> factory;

        const bool fixedIsReporting = getFixedCurrency() == curveCurrency;

        // Choose factory direction depending on the projection curve
        if(curveCurrency == forCurrency)
        {
            factory = new FloatProjYCFactory(domDisc, forDisc, fixedIsReporting);
            ycUsed.domDisc_m = true;
            ycUsed.forDisc_m = true;
            ycUsed.forProj_m = true;
        }
        else if(curveCurrency == domCurrency)
        {
            factory = new FloatProjYCFactory(forDisc, domDisc, fixedIsReporting);
            ycUsed.domDisc_m = true;
            ycUsed.forDisc_m = true;
            ycUsed.domProj_m = true;
        }
        else
            throwFatalException("Can't strip from instrument that "
                                "is independent of projection curve.");

        return getAbstractProduct(domCurrency != forCurrency,
                                  *factory,
                                  nowDate, spotDate,
                                  fixedScale, floatScale,
                                  curveCurrency,
                                  quotes, bump, bumpUsedOut,
                                  fudgeFirstFixing,
                                  warning);
    }

    // NOTE: Use ONLY for single-currency yield curve stripping.
    // This hard codes both leg scales to 1.
    virtual YCProductInstrument::Ptr getYCProduct_impl(const Date& nowDate,
                                                       const IQuotes::CPtr& quotes,
                                                       const Bump::CPtr& bump,
                                                       int& bumpUsedOut,
                                                       bool fudgeFirstFixing,
                                                       const InstrumentYield_I::FXInfo& fxInfo,
                                                       ApplicationWarning& warning) const
    {
        if(getFixedCurrency() != getFloatCurrency())
            throwFatalException("Stripping domestic yield curves from cross-currency swaps is not supported yet.");

        SimpleYCFactory simpleYCFactory;
        return getAbstractProduct(false,
                                  simpleYCFactory,
                                  nowDate, nowDate,
                                  1, 1,
                                  getCurrency(),
                                  quotes, bump, bumpUsedOut,
                                  fudgeFirstFixing,
                                  warning);
    }

    // NOTE: Use ONLY for single-currency yield curve stripping.
    // This hard codes both leg scales to 1.
    virtual YCProductInstrument::Ptr getProjectionProduct(const YieldCurve_I::CPtr& discountCurve,
                                                          const Date& nowDate,
                                                          const IQuotes::CPtr& quotes,
                                                          const Bump::CPtr& bump,
                                                          int& bumpUsedOut,
                                                          ApplicationWarning& warning) const
    {
        IInstrumentStripCC::YCUsed ycUsed;
        return getProjYCProduct(discountCurve, getCurrency(),
                                discountCurve, getCurrency(),
                                nowDate, nowDate,
                                1, 1,
                                getCurrency(),
                                quotes, bump,
                                bumpUsedOut,
                                false,
                                warning,
                                ycUsed);
    }

    virtual FIN_SwapInstrument::Ptr instrument(const Date& now_date,
                                               const IQuotes::CPtr quotes,
                                               const BumpShift* bump,
                                               bool unitNotional,
                                               const CurveTenorInterpolated::Ptr& floatStubIndexCurve,
                                               const Currency& curveCurrency,
                                               int& bump_used_out,
                                               ApplicationWarning& warn,
                                               Date& start_out,
                                               Date& end_out,
                                               bool fudge_first_fixing) const
    {
        double fixedNotional = unitNotional ? -1.0 : fixedNotional_m;
        double floatNotional = unitNotional ? -1.0 : floatNotional_m;

        if(unitNotional && fixedNotional_m != floatNotional_m)
        {
            if(curveCurrency == getFixedCurrency())
                floatNotional = -floatNotional_m/fixedNotional_m;
            else if(curveCurrency == getFloatCurrency())
                fixedNotional = -fixedNotional_m/floatNotional_m;
            else
                throwFatalException("Domestic currency not known by the swap.");
        }

        ScheduleInfo::Ptr schedule = calc_schedule(now_date, fudge_first_fixing);

        if(schedule->fixed_m.dates_m.empty())
            throwAppException("[" + getID() + "] empty dates");

        start_out = schedule->fixed_m.dates_m.front()->getPeriodStartDate();

        FIN_PaymentStream::Ptr fixedLeg(schedule->fixed_m.makeStream(fixedNotional,
                                                                     fixedRate_m->getValue(quotes, bump, bump_used_out),
                                                                     compounding_frequency_m));

        IFloatStream::Ptr
            floatLeg(schedule->float_m.makeStream(floatNotional,
                                                  floatRate_m->getValue(quotes, bump, bump_used_out),
                                                  queryFixingsFunc()));

        FIN_SwapInstrument::Ptr ret;
        ret = new FIN_SwapInstrument(fixedLeg,
                                     floatLeg);
        end_out = ret->getLastDate();
        return ret;
    }

protected:
    virtual bool validate()
    {
        if(!Swap::validate())
            return false;

        return true;
    }

    void parseSingleCcyNotional(const ParseResult& result)
    {
        fixedNotional_m = result.getDouble(tkStructureNotional, 1.0);
        floatNotional_m = fixedNotional_m;
    }

    void parseCrossCcyNotionals(const ParseResult& result)
    {
        fixedNotional_m = result.getDouble(tkFixedNotional);
        floatNotional_m = result.getDouble(tkFloatNotional);
    }

    void parseRates(const ParseResult& result,
                    const TokenType& fixedRateToken)
    {
        fixedRate_m = result.getQuote(fixedRateToken);

        InstrumentQuote::Ptr floatRate = result.queryQuote(tkFloatSpread);
        if(!floatRate)
            floatRate = new InstrumentQuote(makeQuoteName(tkFloatSpread), 0.0);
        floatRate_m = floatRate;
    }

    class ConstantParameterSwapPayoff : public Swap::SwapPayoff
    {
    public:
        ConstantParameterSwapPayoff(bool hasTwoLegs,
                                    double fixedNotional,
                                    const CurrencyType fixedCurrType,
                                    double floatNotional,
                                    const CurrencyType floatCurrType,
                                    bool doFrontStubInterpolation,
                                    bool doBackStubInterpolation,
                                    const pair<double,double>& frontStubRateWeights,
                                    const pair<double,double>& backStubRateWeights) :
        SwapPayoff(fixedCurrType,
                   floatCurrType,
                   doFrontStubInterpolation,
                   doBackStubInterpolation,
                   frontStubRateWeights,
                   backStubRateWeights),
        hasTwoLegs_m(hasTwoLegs),
        fixedNotional_m(fixedNotional),
        floatNotional_m(floatNotional)
        {}

        virtual void doPayoff()
        {
            Underlying fixed_leg = get(coupon_leg_str);

            if(isActive(coupon_date_str))
                doFixedLeg(fixedNotional_m,
                           fixed_leg);

            if(hasTwoLegs_m) {
                Underlying float_leg = get(fund_leg_str);

                if(isActive(fund_date_str))
                    doFloatLeg(floatNotional_m,
                               float_leg);

                if(isCrossCurrency_m)
                    doNotionalExchange(fixedNotional_m,
                                       floatNotional_m,
                                       true, true,
                                       fixed_leg,
                                       float_leg);

                Underlying swap = get(swap_str);
                getSwap(fixed_leg, float_leg,
                        swap);
            }
        }

        virtual Cloneable_I* clone() const
        {
            return new ConstantParameterSwapPayoff(hasTwoLegs_m,
                                                   fixedNotional_m,
                                                   fixedCurrType_m,
                                                   floatNotional_m,
                                                   floatCurrType_m,
                                                   doFrontStubInterpolation_m,
                                                   doBackStubInterpolation_m,
                                                   frontStubRateWeights_m,
                                                   backStubRateWeights_m);
        }

    private:
        bool hasTwoLegs_m;
        double fixedNotional_m;
        double floatNotional_m;
    };

    virtual void registerData(const NXKernel_I::Ptr& kernel,
                              const Bump* bump,
                              int& bumpUsedOut)
    {
        const BumpShift* bumpShift = dynamic_cast<const BumpShift*>(bump);
        kernel->registerData(fixed_str, fixedRate_m->getValue(bumpShift, bumpUsedOut));
        kernel->registerData(float_str, floatRate_m->getValue(bumpShift, bumpUsedOut));
    }

    virtual void registerPayoff(const NXKernel_I::Ptr& kernel,
                                CurrencyType fixedCurrencyType,
                                CurrencyType floatCurrencyType,
                                bool doFrontStubInterpolation,
                                bool doBackStubInterpolation,
                                const pair<double,double>& frontStubRateWeights,
                                const pair<double,double>& backStubRateWeights)
    {
        ConstantParameterSwapPayoff payoff(hasTwoLegs_m,
                                           fixedNotional_m,
                                           fixedCurrencyType,
                                           floatNotional_m,
                                           floatCurrencyType,
                                           doFrontStubInterpolation,
                                           doBackStubInterpolation,
                                           frontStubRateWeights,
                                           backStubRateWeights);

        kernel->registerPayoff(payoff);
    }

protected:
    double fixedNotional_m;
    double floatNotional_m;
    InstrumentQuote::Ptr fixedRate_m;
    InstrumentQuote::Ptr floatRate_m;
};

struct UseRateDates
{
    bool useRateDates_m;

    void parseUseRateDates(const ParseResult& result,
                           ApplicationWarning& warning)
    {
        string calcRule = StringFunctions::fixName(result.getString(tkRateCalcRule, "USE RATE DATES"));
        useRateDates_m = calcRule == "USERATEDATES";
        if(!useRateDates_m && calcRule != "USEACCRUALDATES")
            throwFatalException("Expected USE RATE DATES or USE ACCRUAL DATES for rate calculation rule");

        if(!useRateDates_m && (result.querySingle(tkRateFreq) ||
                               result.querySingle(tkRateSpotLag) ||
                               result.querySingle(tkRateAccrualCal) ||
                               result.querySingle(tkRateAccrualConv) ||
                               result.querySingle(tkRateFixingCal) ||
                               result.querySingle(tkRateFixingConv)))
            warning.setMinor("Using accrual dates for floating rate.  Specified rate inputs are ignored.");
    }
};

/**
 * Base implementation for all new swaps.
 */
class NewSwap : public ConstantParameterSwap, public UseRateDates
{
public:
    struct Check : public ConstantParameterSwap::Check
    {
        FixedLegConvSpec fixed_leg_conv_spec_m;
        FloatLegConvSpec float_leg_conv_spec_m;
        FloatRateConvSpec float_rate_conv_spec_m;

        Check(const char* swapType, const TokenType& ttype, const char* msg, int priority) :
            ConstantParameterSwap::Check(swapType, ttype, msg, priority),
            fixed_leg_conv_spec_m(swapType),
            float_leg_conv_spec_m(swapType),
            float_rate_conv_spec_m(swapType)
        {
            addRequired(tkFixedRate, ParseString | ParseDouble);
            addOptional(tkFloatSpread, ParseString | ParseDouble);

            addOptional(tkFixedCompoundingType, ParseString);

            addOptional(tkFloatStubIndexCurve, ParseID);

            addOptional(float_rate_conv_spec_m, tkRateFixingConv, ParseString, tkFixingConvention);
            addOptional(float_rate_conv_spec_m, tkRateCalcRule, ParseString);
        }
    };

protected:
    virtual bool validate()
    {
        if(!ConstantParameterSwap::validate())
            return false;

        // From now on we always have two legs
        hasTwoLegs_m = true;

        const ParseResult& result = getParameters();
        ApplicationWarning& warning = getWarnings();

        parseUseRateDates(result, warning);
        parseRates(result, tkFixedRate);
        parseCompoundingFrequency(result, tkFixedCompoundingType);

        floatStubIndexCurve_m = result.queryPtr(tkFloatStubIndexCurve);

        return true;
    }

#ifndef RISK_MCVAR_DISABLE
    // For IR Swap instruments with const notional
    virtual AnalyticCapitalTradeBase::Ptr getAnalyticCapitalInfoIRSwap(
        const Date& valueDate,
        double marketValue,
        bool position,
        const string& id,
        ApplicationWarning& warning
        )
    {
        string tradeType = AnalyticCapitalTradeBase::tradeRegularStr();

        double notional = getNotional();
        string ccy = getCurrency().toString();

        Date startDate, endDate;
        getStartAndEndDates(valueDate, startDate, endDate);

        return Shared_ptr<AnalyticCapitalTradeIR>(new AnalyticCapitalTradeIR(
            ccy,
            endDate,
            marketValue,
            notional,
            position,
            startDate,
            endDate,
            tradeType,
            id)
            );
    }

    // For IR CC Swap instruments with const notional (treated as FX)
    virtual AnalyticCapitalTradeBase::Ptr getAnalyticCapitalInfoIRCCSwap(
        const Date& valueDate,
        double marketValue,
        bool position,
        const string& id,
        ApplicationWarning& warning
        )
    {
        string tradeType = AnalyticCapitalTradeBase::tradeRegularStr();

        double notional = getNotional();
        string ccy1 = getFixedCurrency().toString();

        Date startDate, endDate;
        getStartAndEndDates(valueDate, startDate, endDate);

        string ccy2 = getFloatCurrency().toString();
        return Shared_ptr<AnalyticCapitalTradeFX>(new AnalyticCapitalTradeFX(
            ccy1,
            endDate,
            marketValue,
            notional,
            position,
            floatNotional_m,
            ccy2,
            tradeType,
            id)
            );
    }
#endif

};

struct NewFixedLegCheck : public FixedLegCheck
{
    NewFixedLegCheck()
    {
        tk_basis = tkFixedAccrualBasis;
    }
};

struct NewFloatLegCheck : public FloatLegCheck
{
    NewFloatLegCheck()
    {
        tk_basis = tkFloatAccrualBasis;
        tk_freq = tkFloatAccrualFreq;
    }
};

const NewFixedLegCheck new_fixed_leg_check_s;
const NewFloatLegCheck new_float_leg_check_s;

class NewSwapTwoLegs : public NewSwap
{
public:
    struct Check : public NewSwap::Check
    {
        Check(const char* swapType, const TokenType& ttype, const char* msg, int priority, bool includeStubFlags = true) :
            NewSwap::Check(swapType, ttype, msg, priority)
        {
            new_fixed_leg_check_s.addChecks(*this, fixed_leg_conv_spec_m, includeStubFlags);
            new_float_leg_check_s.addChecks(*this, float_leg_conv_spec_m, includeStubFlags);

            addOptional(tkStartDate, ParseString | ParseDate);
            addOptional(tkEventsRefDate, ParseString | ParseDate);
            addRequired(tkMaturityDate, ParseString | ParseDate);

            addOptional(fixed_leg_conv_spec_m, tkFixedEndOfMonth, ParseBool, tkConventionEndOfMonth);
            addOptional(float_leg_conv_spec_m, tkFloatEndOfMonth, ParseBool, tkConventionEndOfMonth);
            addOptional(float_leg_conv_spec_m, tkFloatFixingLag, ParseString, tkEventsFixingLag);
            addOptional(float_leg_conv_spec_m, tkFloatFixingCalendar, ParseString, tkFixingCalendar);
            addOptional(float_leg_conv_spec_m, tkFloatFixingConvention, ParseString, tkFixingConvention);

            addOptional(float_rate_conv_spec_m, tkRateAccrualCal, ParseString, tkEventsAccrualCalendar);
            addOptional(float_rate_conv_spec_m, tkRateAccrualConv, ParseString, tkEventsAccrualConvention);
            addOptional(float_rate_conv_spec_m, tkRateFreq, ParseString, tkCommonFrequency);
            addOptional(float_rate_conv_spec_m, tkRateSpotLag, ParseString, tkEventsFixingLag);
            addOptional(float_rate_conv_spec_m, tkRateFixingCal, ParseString, tkFixingCalendar);
            addOptional(float_rate_conv_spec_m, tkRateBasis, ParseString, tkCommonBasis);
            addOptional(float_rate_conv_spec_m, tkRateEndOfMonth, ParseBool, tkConventionEndOfMonth);
        }
    };

    bool hasMaturityTenor() const
    {
        return maturity_m.isNotDateBased();
    }

    Option<Tenor> getMaturityTenor() const
    {
        return Option<Tenor>(*(maturity_m.asTenor()));
    }

    Maturity getMaturity() const
    {
        return maturity_m;
    }

protected:
    virtual bool validate()
    {
        if(!NewSwap::validate())
            return false;

        const ParseResult& result = getParameters();

        refDate_m = result.queryDate(tkEventsRefDate);

        floatFixingLag_m = result.getTenor(tkFloatFixingLag, zero_bd_s);

        fixed_leg_m.init(result, new_fixed_leg_check_s, NULL);
        float_leg_m.init(result, new_float_leg_check_s, &fixed_leg_m);

        string floatFixingCalendarName = result.getString(tkFloatFixingCalendar, none_s);

        fixedEndOfMonth_m = result.getBool(tkFixedEndOfMonth, false);
        floatEndOfMonth_m = result.getBool(tkFloatEndOfMonth, fixedEndOfMonth_m);
        rateEndOfMonth_m = result.getBool(tkRateEndOfMonth, floatEndOfMonth_m);

        start_m = result.getStart(
            tkStartDate, tkCommonFailed, tkMaturityDate,
            floatFixingLag_m.toString(), "F",
            floatFixingCalendarName, float_leg_m.acc_cal_name_m);

        maturity_m = result.getMaturity(
            tkMaturityDate, "NONE", "NONE", floatEndOfMonth_m);

        const string float_fix_conv = result.getString(tkFloatFixingConvention, "P");
        floatFixingCal_m = result.getCalendar(float_fix_conv, floatFixingCalendarName);

        const string acc_cal = result.getString(tkRateAccrualCal, float_leg_m.acc_cal_name_m);
        const string acc_conv = result.getString(tkRateAccrualConv, float_leg_m.acc_conv_name_m);

        rateAccrualCal_m = result.getCalendar(acc_conv, acc_cal);

        rateFrequency_m = result.getTenor(tkRateFreq, float_leg_m.freq_m);
        if (rateFrequency_m != float_leg_m.freq_m)
            getWarnings().setMinor("Rate frequency is different from the frequency of the floating leg.  Expect convexity effects.");

        if (rateEndOfMonth_m && !rateFrequency_m.inMonths() && !rateFrequency_m.inYears()) {
            rateEndOfMonth_m = false; // flip it if frequency is non-compliant
            getWarnings().setMinor("EOM flag non-compliant with rate frequency, flipped to FALSE.");
        }

        rateSpotLag_m = result.getTenor(tkRateSpotLag, floatFixingLag_m);

        const string fix_cal = result.getString(tkRateFixingCal, floatFixingCalendarName);
        const string rate_fix_conv = result.getString(tkRateFixingConv, "F");

        rateFixingCal_m = result.getCalendar(rate_fix_conv, fix_cal);

        rateBasisType_m = result.getBasis(tkRateBasis, float_leg_m.basisType_m);

        return true;
    }

    virtual ScheduleInfo::Ptr calc_schedule(const Date& now_date,
                                            bool fudge_first_fixing) const
    {
        Date start = start_m.getDate(now_date);
        Date end = maturity_m.getDate(start);

        ScheduleMaker fixed_maker(
            nominal_cal_m.get(),
            start, end, fixed_leg_m.freq_m,
            fixed_leg_m.front_stub_m, fixed_leg_m.long_stub_m,
            true, true, false,
            fixed_leg_m.pay_cal_m.get(),
            Tenor("0d"), false,
            fixed_leg_m.pay_cal_m.get(), fixed_leg_m.pay_lag_m,
            fixed_leg_m.acc_cal_m.get(),
            fixedEndOfMonth_m);

        ScheduleMaker float_maker(
            nominal_cal_m.get(),
            start, end, float_leg_m.freq_m,
            float_leg_m.front_stub_m, float_leg_m.long_stub_m,
            true, true, false,
            floatFixingCal_m.get(),
            floatFixingLag_m, false,
            float_leg_m.pay_cal_m.get(), float_leg_m.pay_lag_m,
            float_leg_m.acc_cal_m.get(),
            floatEndOfMonth_m);

        const vector<Date>* fixedBaDates = &fixed_maker.getBondAccrualDates();
        bool fixedEom = fixed_maker.isEOMDated();
        Basis_I::CPtr fixedBasis = fixed_leg_m.getBasis(fixedBaDates, &end, &fixedEom);

        const vector<Date>* floatBaDates = &float_maker.getBondAccrualDates();
        bool floatEom = float_maker.isEOMDated();
        Basis_I::CPtr floatBasis = float_leg_m.getBasis(floatBaDates, &end, &floatEom);

        const Basis_I::CPtr& rateBasis = InstrumentBase::getBasis(
            rateBasisType_m, rateAccrualCal_m,
            floatBaDates, &rateFrequency_m, &end, &floatEom);

        return ScheduleInfo::Ptr(new ScheduleInfo(fixed_maker.getDates(),
                                                  fixedBasis,
                                                  float_maker.getDates(),
                                                  floatBasis,
                                                  rateBasis,
                                                  useRateDates_m,
                                                  rateAccrualCal_m,
                                                  rateFrequency_m,
                                                  rateFixingCal_m,
                                                  rateSpotLag_m,
                                                  rateEndOfMonth_m,
                                                  fudge_first_fixing));
    }

    virtual void doRegistration(const NXKernel_I::Ptr& kernel,
                                Model_I::PtrCRef model,
                                const Bump* bump,
                                ApplicationWarning& warning,
                                int& bumpUsedOut)
    {
        doRegistrationTwoLegs(kernel, model, bump, warning, bumpUsedOut,
                              fixed_leg_m.basisType_m,
                              float_leg_m.basisType_m,
                              rateBasisType_m,
                              useRateDates_m? &rateFrequency_m : NULL,
                              rateAccrualCal_m.get(),
                              &rateSpotLag_m,
                              rateFixingCal_m.get());
    }

public:

    /// <summary>
    /// Return interfaces implemented by object
    /// </summary>
    /// <param name="container">Container into which to possibly create values</param>
    /// <param name="interfaces">[out] Interfaces implemented by object</param>
    /// <param name="syntheticID">ID or synthetic ID identifying synthetic parts</param>
    /// <returns>Whether object implements any interfaces</returns>
    virtual bool implements(const RegisteredObject *container, StringVectorRef interfaces, StringCRef syntheticID) const
    {
        interfaces = interfaces_s;

        StringVector iFaces;
        this->InstrumentBase::implements(container, iFaces, syntheticID);

        IInterface::blend(interfaces, iFaces);
        return true;
    }

    virtual bool getValues(StringCRef iFace, Vector<ApplicationVariant> &results, ApplicationWarning &warning)
    {
        SWITCH_BLOCK(String, StringFunctions::fixName(iFace))
        {
            CASE_ONE("MATURITY")
            {
                results.push_back(maturity_m.toString());
                return true;
            }
            DEFAULT_STATEMENT
                warning.setFatal("\"" + iFace + "\" invalid interface for "
                    + String(InstrumentIRSwap::typeID_s) + " "
                    + String(InstrumentIRSwap::baseID_s) + " object");
        }
        return false;
    }

    virtual void getStartAndEndDates(
        const Date& nowDate,
        Date& start,
        Date& end) const
    {
        start = start_m.isValid() ? start_m.getDate(nowDate) : nowDate;
        if (start < nowDate)
            start = nowDate;

        end = maturity_m.getDate(start);
        if (end < nowDate)
            end = nowDate;
    }

protected:
    Date refDate_m;
    Start start_m;
    Maturity maturity_m;

    LegCal fixed_leg_m;
    LegCal float_leg_m;

    bool fixedEndOfMonth_m;
    bool floatEndOfMonth_m;
    bool rateEndOfMonth_m;

    Calendar_I::CPtr rateAccrualCal_m;
    Calendar_I::CPtr floatFixingCal_m;
    Calendar_I::CPtr rateFixingCal_m;
    Tenor floatFixingLag_m;
    Tenor rateFrequency_m;
    Tenor rateSpotLag_m;
    BasisType rateBasisType_m;
};

struct FromEvents
{
    vector<EventSchedule_I::IPeriod::CPtr> fixedPeriods_m;
    vector<EventSchedule_I::IPeriod::CPtr> floatPeriods_m;

    Basis_I::CPtr fixedBasis_m;
    Basis_I::CPtr floatBasis_m;
    Basis_I::CPtr rateBasis_m;

    BasisType basisType_m;
    BasisType floatBasisType_m;

    string accCal_m;
    string accConv_m;

    string fixCal_m;
    string fixConv_m;

    Tenor rateFrequency_m;
    Tenor rateSpotLag_m;
    bool rateEndOfMonth_m;
    Calendar_I::CPtr rateFixingCal_m;
    Calendar_I::Ptr rateAccrualCal_m;

    void parseEventsRelatedHeaders(const ParseResult& result, ApplicationWarning& warning)
    {
        basisType_m = result.getBasis(tkFixedAccrualBasis);
        floatBasisType_m = result.getBasis(tkFloatAccrualBasis, basisType_m);

        accCal_m = result.getString(tkRateAccrualCal, none_s);
        accConv_m = result.getString(tkRateAccrualConv, "F");
        rateAccrualCal_m = result.getCalendar(accConv_m, accCal_m);

        rateFrequency_m = result.getTenor(tkRateFreq);
        rateSpotLag_m = result.getTenor(tkRateSpotLag, zero_bd_s);

        rateEndOfMonth_m = result.getBool(tkRateEndOfMonth, false);
        if(rateEndOfMonth_m && !rateFrequency_m.inMonths() && !rateFrequency_m.inYears()) {
            rateEndOfMonth_m = false; // flip it if frequency is non-compliant
            warning.setMinor("EOM flag non-compliant with rate frequency, flipped to FALSE.");
        }

        fixCal_m = result.getString(tkRateFixingCal, none_s);
        fixConv_m = result.getString(tkRateFixingConv, "F");

        rateFixingCal_m = result.getCalendar(fixConv_m, fixCal_m);

        rateBasis_m = result.getBasis_I(tkRateBasis, true);
    }

    void setMinorWarnings(const EventsInterestBase::Ptr& floatEvents,
                          ApplicationWarning& warning)
    {
        if(floatEvents->hasAccrualCalendar() && accCal_m != floatEvents->getAccrualCalendar())
            warning.setMinor("Rate accrual calendar differs from that of the events schedule");
        if(floatEvents->hasAccrualConvention() && accConv_m != floatEvents->getAccrualConvention())
            warning.setMinor("Rate accrual roll convention differs from that of the floating events schedule");
        if(floatEvents->hasAccrualFrequency() && rateFrequency_m != floatEvents->getAccrualFrequency())
            warning.setMinor("Rate frequency is different from the frequency of the floating events schedule.  Expect convexity effects");
        if(floatEvents->hasFixingLag() && rateSpotLag_m != floatEvents->getFixingLag())
            warning.setMinor("Rate spot lag is different from the fixing lag of the floating events schedule");
        if(floatEvents->hasFixingCalendarName() && fixCal_m != floatEvents->getFixingCalendarName())
            warning.setMinor("Rate fixing calendar differs from that of the floating events schedule");
    }
};

class NewSwapFromEvents : public NewSwap, public FromEvents
{
public:
    struct Check : public NewSwap::Check
    {
        Check(const char* swapType, const TokenType& ttype, const char* msg, int priority) :
            NewSwap::Check(swapType, ttype, msg, priority)
        {
            addRequired(tkInstrumentFixedEvents, ParseID);
            addRequired(tkFloatEvents, ParseID);

            addRequired(fixed_leg_conv_spec_m, tkFixedAccrualBasis, ParseString, tkCommonBasis);
            addOptional(float_leg_conv_spec_m, tkFloatAccrualBasis, ParseString, tkCommonBasis);

            addRequired(float_rate_conv_spec_m, tkRateAccrualCal, ParseString, tkEventsAccrualCalendar);
            addRequired(float_rate_conv_spec_m, tkRateAccrualConv, ParseString, tkEventsAccrualConvention);
            addRequired(float_rate_conv_spec_m, tkRateFreq, ParseString, tkCommonFrequency);
            addRequired(float_rate_conv_spec_m, tkRateSpotLag, ParseString, tkEventsFixingLag);
            addRequired(float_rate_conv_spec_m, tkRateFixingCal, ParseString, tkFixingCalendar);
            addRequired(float_rate_conv_spec_m, tkRateBasis, ParseString, tkCommonBasis);
            addOptional(float_rate_conv_spec_m, tkRateEndOfMonth, ParseBool, tkConventionEndOfMonth);
        }
    };

protected:
    virtual bool validate()
    {
        if(!NewSwap::validate())
            return false;

        const ParseResult& result = getParameters();
        ApplicationWarning& warning = getWarnings();

        parseEventsRelatedHeaders(result, warning);

        fixedBasis_m = Basis_I::createBasis(basisType_m);
        floatBasis_m = Basis_I::createBasis(floatBasisType_m);

        parseEvents(result, tkInstrumentFixedEvents, basisType_m, fixedPeriods_m, fixedBasis_m, warning);
        parseEvents(result, tkFloatEvents, floatBasisType_m, floatPeriods_m, floatBasis_m, warning);

        RegisteredObject::Ptr floatObj = result.getPtr(tkFloatEvents);
        EventsInterestBase::Ptr floatEvents = dyn_cast<EventsInterestBase>(floatObj);
        if(floatEvents)
            setMinorWarnings(floatEvents,
                             warning);
        const Date temp;
        schedule_m = calc_schedule(temp, false);

        return true;
    }

    virtual ScheduleInfo::Ptr calc_schedule(const Date& now_date,
                                            bool fudge_first_fixing) const
    {
        return ScheduleInfo::Ptr(new ScheduleInfo(fixedPeriods_m,
                                                  fixedBasis_m,
                                                  floatPeriods_m,
                                                  floatBasis_m,
                                                  rateBasis_m,
                                                  useRateDates_m,
                                                  rateAccrualCal_m,
                                                  rateFrequency_m,
                                                  rateFixingCal_m,
                                                  rateSpotLag_m,
                                                  rateEndOfMonth_m));
    }

    virtual void doRegistration(const NXKernel_I::Ptr& kernel,
                                Model_I::PtrCRef model,
                                const Bump* bump,
                                ApplicationWarning& warning,
                                int& bumpUsedOut)
    {
        doRegistrationTwoLegs(kernel, model, bump, warning, bumpUsedOut,
                              schedule_m->fixed_m.basis_m->getBasisType(),
                              schedule_m->float_m.floatBasis_m->getBasisType(),
                              schedule_m->float_m.rateBasis_m->getBasisType(),
                              useRateDates_m? &schedule_m->float_m.freq_m : NULL,
                              schedule_m->float_m.acc_cal_m.get(),
                              &schedule_m->float_m.fix_lag_m,
                              schedule_m->float_m.fixing_cal_m.get());
    }

    virtual void getStartAndEndDates(
        const Date& nowDate,
        Date& start,
        Date& end) const
    {
        getStartAndEndDatesForFixedFloatSwap(nowDate, start, end);
    }

    virtual bool hasMaturityTenor() const { return false; }

protected:

    virtual void view(ApplicationData& out) const {
        viewWithSchedule(out, schedule_m); 
    }

	ScheduleInfo::Ptr schedule_m;
};

class NewSwapTwoLegsSingleCcy : public NewSwapTwoLegs
{
public:
    struct Check : public NewSwapTwoLegs::Check
    {
        typedef NewSwapTwoLegsSingleCcy ObjectType;
        Check() : NewSwapTwoLegs::Check(irswap_s, tkIRSwap, "IR Swap from start/maturity and explicit legs", 10)
        {
            addRequired(tkCurrency, ParseString);
            addOptional(tkStructureNotional, ParseDouble);
        }
    };

    virtual bool validate()
    {
        if(!NewSwapTwoLegs::validate())
            return false;

        const ParseResult& result = getParameters();
        parseSingleCcyNotional(result);

        if(refDate_m.isValid() || start_m.isDate()) 
        {
            schedule_m = NewSwapTwoLegs::calc_schedule(refDate_m, false);
        }

        return true;
    }

#ifndef RISK_MCVAR_DISABLE
    virtual AnalyticCapitalTradeBase::Ptr getAnalyticCapitalInfo(
        const Date& valueDate,
        double marketValue,
        bool position,
        const string& id,
        const DataInput::CPtr& instrInput,
        ApplicationWarning& warning
        )
    {
        return getAnalyticCapitalInfoIRSwap(valueDate, marketValue, position, id, warning);
    }
#endif

protected:


    virtual ScheduleInfo::Ptr calc_schedule(
        const Date& now_date,
        bool fudge_first_fixing) const 
    {
        // For this parse check, the presence of a precomputed schedule implies that this is
        // a "frozen" schedule that doesn't vary with the now date (But it should still
        // respect the fudge_first_fixing flag.
        //
        // If the precomputed schedule is not present, it is a floating instrument.
        if (schedule_m) 
        {
            if (!fudge_first_fixing)
            {
                // These are the same conditions under which the original schedule was computed.
                return schedule_m;
            }
            else
            {
                // Create a schedule with the same ref date, but respecting the new flags.
                return NewSwapTwoLegs::calc_schedule(refDate_m, fudge_first_fixing);
            }
        }
        else
        {
            // All bets are off.  Cache is totally invalid.  Recompute everything.
            return NewSwapTwoLegs::calc_schedule(now_date, fudge_first_fixing);
        }
    }

    virtual void view(ApplicationData& out) const {
        if (schedule_m) {  viewWithSchedule(out, schedule_m); }
    }

	ScheduleInfo::Ptr schedule_m;

};

class NewSwapTwoLegsRollTypeSingleCcy : public NewSwapTwoLegs
{
public:
    struct Check : public NewSwapTwoLegs::Check
    {
        typedef NewSwapTwoLegsRollTypeSingleCcy ObjectType;
        Check() : NewSwapTwoLegs::Check(irswap_s, tkIRSwap, "IR Swap from start/maturity/roll and explicit legs", 10, false)
        {
            addRequired(tkCurrency, ParseString);
            addOptional(tkStructureNotional, ParseDouble);

            addOptional(tkFixedRollDate, ParseDate | ParseString);
            addOptional(tkFixedRollDateType, ParseString);
            addOptional(tkFixedFrontStubType, ParseString);
            addOptional(tkFixedBackStubType, ParseString);

            addOptional(tkFloatRollDate, ParseDate | ParseString);
            addOptional(tkFloatRollDateType, ParseString);
            addOptional(tkFloatFrontStubType, ParseString);
            addOptional(tkFloatBackStubType, ParseString);
        }
    };

    void validateRollDateType()
    {
        const ParseResult& result = getParameters();

        // RollDate is optional for the IMM schedule so use queryDate as
        // this returns an uninitialized Date if ROLL DATE isn't specified.
        fixedRollDate_m = result.queryDate(tkFixedRollDate);
        floatRollDate_m = result.queryDate(tkFloatRollDate);
        string fixedRollDateType = StringFunctions::fixName(
            result.getString(tkFixedRollDateType,"REGULAR"));
        string floatRollDateType = StringFunctions::fixName(
            result.getString(tkFloatRollDateType,"REGULAR"));

        fixedRollDateType_m = RegularRollDate;
        if (fixedRollDateType == "IMM")
            fixedRollDateType_m = IMMThirdWednesdayRollDate;
        else if (fixedRollDateType != "REGULAR")
            throwAppException("FIXED ROLL DATE TYPE must be REGULAR or IMM");

        floatRollDateType_m = RegularRollDate;
        if (floatRollDateType == "IMM")
            floatRollDateType_m = IMMThirdWednesdayRollDate;
        else if (floatRollDateType != "REGULAR")
            throwAppException("FLOAT ROLL DATE TYPE must be REGULAR or IMM");

        if (!fixedRollDate_m.isValid())
        {
            if (fixedRollDateType != "IMM")
                throwAppException(
                    "FIXED ROLL DATE must be specified if FIXED ROLL DATE TYPE is REGULAR");
            else if (fixed_leg_m.freq_m != Tenor(3,"m"))
            {
                throwAppException(
                    "FIXED ROLL DATE must be specified for IMM schedules if the "
                    "period is greater than 3m");
            }
        }

        if (!floatRollDate_m.isValid())
        {
            if (floatRollDateType != "IMM")
                throwAppException(
                    "FLOAT ROLL DATE must be specified if FLOAT ROLL DATE TYPE is REGULAR");
            else if (float_leg_m.freq_m != Tenor(3,"m"))
            {
                throwAppException(
                    "FLOAT ROLL DATE must be specified for IMM schedules if the "
                    "period is greater than 3m");
            }
        }

        string tmp;
        tmp = result.getString(tkFixedFrontStubType, "LONG");
        fixedFrontStubType_m = EventsHelper_RollDate::getStubType(tmp);
        tmp = result.getString(tkFloatFrontStubType, "LONG");
        floatFrontStubType_m = EventsHelper_RollDate::getStubType(tmp);

        tmp = result.getString(tkFixedBackStubType, "NONE");
        fixedBackStubType_m = EventsHelper_RollDate::getStubType(tmp);
        tmp = result.getString(tkFloatBackStubType, "NONE");
        floatBackStubType_m = EventsHelper_RollDate::getStubType(tmp);
    }

    virtual bool validate()
    {
        if(!NewSwapTwoLegs::validate())
            return false;

        const ParseResult& result = getParameters();
        parseSingleCcyNotional(result);

        validateRollDateType();

        if(refDate_m.isValid() || start_m.isDate())
        {
            schedule_m = calc_schedule(refDate_m, false);
        }

        return true;
    }

    virtual ScheduleInfo::Ptr calc_schedule(const Date& now_date,
                                            bool fudge_first_fixing) const
    {
        // For this parse check, the presence of a precomputed schedule implies that this is
        // a "frozen" schedule that doesn't vary with the now date (But it should still
        // respect the fudge_first_fixing flag.
        //
        // If the precomputed schedule is not present, it is a floating instrument.
        if (schedule_m && !fudge_first_fixing) 
        {
            return schedule_m;
        }
        else
        {
            Date refDate = schedule_m ? refDate_m : now_date;

            Date start = start_m.getDate(refDate);
            Date end = maturity_m.getDate(start);

            ScheduleMaker fixed_maker(nominal_cal_m.get(),
                                      start, end, fixedRollDate_m,
                                      fixedRollDateType_m,
                                      fixed_leg_m.freq_m,
                                      fixedFrontStubType_m, fixedBackStubType_m,
                                      true, true, false,
                                      fixed_leg_m.pay_cal_m.get(),
                                      Tenor("0d"), false,
                                      fixed_leg_m.pay_cal_m.get(), fixed_leg_m.pay_lag_m,
                                      fixed_leg_m.acc_cal_m.get(),
                                      fixedEndOfMonth_m);

            ScheduleMaker float_maker(nominal_cal_m.get(),
                                      start, end, floatRollDate_m,
                                      floatRollDateType_m,
                                      float_leg_m.freq_m,
                                      floatFrontStubType_m, floatBackStubType_m,
                                      true, true, false,
                                      floatFixingCal_m.get(),
                                      floatFixingLag_m, false,
                                      float_leg_m.pay_cal_m.get(), float_leg_m.pay_lag_m,
                                      float_leg_m.acc_cal_m.get(),
                                      floatEndOfMonth_m);

            const vector<Date>* fixedBaDates = &fixed_maker.getBondAccrualDates();
            bool fixedEom = fixed_maker.isEOMDated();
            Basis_I::CPtr fixedBasis = fixed_leg_m.getBasis(fixedBaDates, &end, &fixedEom);

            const vector<Date>* floatBaDates = &float_maker.getBondAccrualDates();
            bool floatEom = float_maker.isEOMDated();
            Basis_I::CPtr floatBasis = float_leg_m.getBasis(floatBaDates, &end, &floatEom);

            const Basis_I::CPtr& rateBasis = InstrumentBase::getBasis(
                rateBasisType_m, rateAccrualCal_m,
                floatBaDates, &rateFrequency_m, &end, &floatEom);

            return ScheduleInfo::Ptr(new ScheduleInfo(fixed_maker.getDates(),
                                                      fixedBasis,
                                                      float_maker.getDates(),
                                                      floatBasis,
                                                      rateBasis,
                                                      useRateDates_m,
                                                      rateAccrualCal_m,
                                                      rateFrequency_m,
                                                      rateFixingCal_m,
                                                      rateSpotLag_m,
                                                      rateEndOfMonth_m,
                                                      fudge_first_fixing));
        }
    }

    virtual void view(ApplicationData& out) const {
        if (schedule_m) {  viewWithSchedule(out, schedule_m); }
    }


#ifndef RISK_MCVAR_DISABLE
    virtual AnalyticCapitalTradeBase::Ptr getAnalyticCapitalInfo(
        const Date& valueDate,
        double marketValue,
        bool position,
        const string& id,
        const DataInput::CPtr& instrInput,
        ApplicationWarning& warning
        )
    {
        return getAnalyticCapitalInfoIRSwap(valueDate, marketValue, position, id, warning);
    }
#endif


private:
    Date fixedRollDate_m;
    Date floatRollDate_m;
    RollDateType fixedRollDateType_m;
    RollDateType floatRollDateType_m;
    StubType fixedFrontStubType_m;
    StubType floatFrontStubType_m;
    StubType fixedBackStubType_m;
    StubType floatBackStubType_m;
    ScheduleInfo::Ptr schedule_m;
};

class NewSwapFromEventsSingleCcy : public NewSwapFromEvents
{
public:
    struct Check : public NewSwapFromEvents::Check
    {
        typedef NewSwapFromEventsSingleCcy ObjectType;
        Check() : NewSwapFromEvents::Check(irswap_s, tkIRSwap, "IR Swap from event schedules", 20)
        {
            addRequired(tkCurrency, ParseString);
            addOptional(tkStructureNotional, ParseDouble);
        }
    };

    virtual bool validate()
    {
        if(!NewSwapFromEvents::validate())
            return false;

        const ParseResult& result = getParameters();
        parseSingleCcyNotional(result);

        return true;
    }

#ifndef RISK_MCVAR_DISABLE
    virtual AnalyticCapitalTradeBase::Ptr getAnalyticCapitalInfo(
        const Date& valueDate,
        double marketValue,
        bool position,
        const string& id,
        const DataInput::CPtr& instrInput,
        ApplicationWarning& warning
        )
    {
        return getAnalyticCapitalInfoIRSwap(valueDate, marketValue, position, id, warning);
    }
#endif

    virtual bool hasMaturityTenor() const { return false; }
};

struct TwoCurrencies
{
    const Currency* fixed_m;
    const Currency* float_m;

    void parseCurrencies(const ParseResult& result) {
        fixed_m = &result.getCurrency(tkFixedCurrency);
        if (*fixed_m == Currency::Failed)
            throwAppException("[" + *fixed_m + "] invalid currency code.");

        float_m = &result.getCurrency(tkFloatCurrency);
        if (*float_m == Currency::Failed)
            throwAppException("[" + *float_m + "] invalid currency code.");
    }
};

class NewSwapTwoLegsCrossCcy : public NewSwapTwoLegs, public TwoCurrencies
{
public:
    struct Check : public NewSwapTwoLegs::Check
    {
        typedef NewSwapTwoLegsCrossCcy ObjectType;
        Check() : NewSwapTwoLegs::Check(ccirswap_s, tkCCIRSwap, "IR Cross-currency Swap from start/maturity and explicit legs", 10)
        {
            addRequired(fixed_leg_conv_spec_m, tkFixedCurrency, ParseString, tkCurrency);
            addRequired(float_leg_conv_spec_m, tkFloatCurrency, ParseString, tkCurrency);

            addRequired(tkFixedNotional, ParseDouble);
            addRequired(tkFloatNotional, ParseDouble);
        }
    };

    virtual bool validate()
    {
        if(!NewSwapTwoLegs::validate())
            return false;

        const ParseResult& result = getParameters();

        parseCurrencies(result);
        parseCrossCcyNotionals(result);

        if(refDate_m.isValid() || start_m.isDate())
        {
            schedule_m = NewSwapTwoLegs::calc_schedule(refDate_m, false);
        }

        return true;
    }

    virtual const Currency& getFixedCurrency() const
    {
        return *fixed_m;
    }

    virtual const Currency& getFloatCurrency() const
    {
        return *float_m;
    }

    virtual bool currencyIsRequired() const
    {
        return false;
    }

#ifndef RISK_MCVAR_DISABLE
    virtual AnalyticCapitalTradeBase::Ptr getAnalyticCapitalInfo(
        const Date& valueDate,
        double marketValue,
        bool position,
        const string& id,
        const DataInput::CPtr& instrInput,
        ApplicationWarning& warning
        )
    {
        return getAnalyticCapitalInfoIRCCSwap(valueDate, marketValue, position, id, warning);
    }
#endif

protected:

    virtual ScheduleInfo::Ptr calc_schedule(
        const Date& now_date,
        bool fudge_first_fixing) const 
    {
        if (schedule_m) {
            if (!fudge_first_fixing)
            {
                return schedule_m;
            }
            else
            {
                return NewSwapTwoLegs::calc_schedule(refDate_m, fudge_first_fixing);
            }
            
        }
        else 
        {
            return NewSwapTwoLegs::calc_schedule(now_date, fudge_first_fixing);
        }
         
    }

    virtual void view(ApplicationData& out) const {
        if (schedule_m) {  viewWithSchedule(out, schedule_m); }
    }

	ScheduleInfo::Ptr schedule_m;
};

class NewSwapFromEventsCrossCcy : public NewSwapFromEvents, public TwoCurrencies
{
public:
    struct Check : public NewSwapFromEvents::Check
    {
        typedef NewSwapFromEventsCrossCcy ObjectType;
        Check() : NewSwapFromEvents::Check(ccirswap_s, tkCCIRSwap, "IR Cross-currency Swap from event schedules", 20)
        {
            addRequired(fixed_leg_conv_spec_m, tkFixedCurrency, ParseString, tkCurrency);
            addRequired(float_leg_conv_spec_m, tkFloatCurrency, ParseString, tkCurrency);

            addRequired(tkFixedNotional, ParseDouble);
            addRequired(tkFloatNotional, ParseDouble);
        }
    };

    virtual bool validate()
    {
        if(!NewSwapFromEvents::validate())
            return false;

        const ParseResult& result = getParameters();

        parseCurrencies(result);
        parseCrossCcyNotionals(result);

        return true;
    }

    const Currency& getFixedCurrency() const
    {
        return *fixed_m;
    }

    const Currency& getFloatCurrency() const
    {
        return *float_m;
    }

    virtual bool currencyIsRequired() const
    {
        return false;
    }

#ifndef RISK_MCVAR_DISABLE
    virtual AnalyticCapitalTradeBase::Ptr getAnalyticCapitalInfo(
        const Date& valueDate,
        double marketValue,
        bool position,
        const string& id,
        const DataInput::CPtr& instrInput,
        ApplicationWarning& warning
        )
    {
        return getAnalyticCapitalInfoIRCCSwap(valueDate, marketValue, position, id, warning);
    }
#endif
};

class TimeDependentSwap : public Swap, public FromEvents, public UseRateDates
{
public:
    struct Check : public Swap::Check
    {
        FixedLegConvSpec fixed_leg_conv_spec_m;
        FloatLegConvSpec float_leg_conv_spec_m;
        FloatRateConvSpec float_rate_conv_spec_m;

        Check(const char* swapType, const TokenType& ttype, const char* msg, int priority) :
            Swap::Check(swapType, ttype, msg, priority),
            fixed_leg_conv_spec_m(swapType),
            float_leg_conv_spec_m(swapType),
            float_rate_conv_spec_m(swapType)
        {
            addRequired(tkInstrumentFixedEvents, ParseID);
            addRequired(tkFloatEvents, ParseID);

            addRequired(tkFixedNotionalTable, ParseID | ParseDouble);
            addOptional(tkFloatNotionalTable, ParseID | ParseDouble);
            addOptional(tkFixedRateTable,     ParseID | ParseDouble);
            addOptional(tkFixedCashflowTable, ParseID);
            addOptional(tkFloatSpreadTable,   ParseID | ParseDouble);

            addOptional(tkFixedCompoundingType, ParseString);

            addOptional(tkFloatStubIndexCurve, ParseID);

            addRequired(fixed_leg_conv_spec_m, tkFixedAccrualBasis, ParseString, tkCommonBasis);
            addOptional(float_leg_conv_spec_m, tkFloatAccrualBasis, ParseString, tkCommonBasis);

            addRequired(float_rate_conv_spec_m, tkRateFreq, ParseString, tkCommonFrequency);
            addRequired(float_rate_conv_spec_m, tkRateBasis, ParseString, tkCommonBasis);
            addOptional(float_rate_conv_spec_m, tkRateFixingConv, ParseString, tkFixingConvention);
            addOptional(float_rate_conv_spec_m, tkRateCalcRule, ParseString);
            addOptional(float_rate_conv_spec_m, tkRateAccrualCal, ParseString, tkEventsAccrualCalendar);
            addOptional(float_rate_conv_spec_m, tkRateAccrualConv, ParseString, tkEventsAccrualConvention);
            addOptional(float_rate_conv_spec_m, tkRateSpotLag, ParseString, tkEventsFixingLag);
            addOptional(float_rate_conv_spec_m, tkRateFixingCal, ParseString, tkFixingCalendar);
            addOptional(float_rate_conv_spec_m, tkRateEndOfMonth, ParseBool, tkConventionEndOfMonth);
        }
    };

    virtual bool constantNotional() const
    {
        return false;
    }

#ifndef RISK_MCVAR_DISABLE
    // Average notional for a fixed leg of a single ccy swap.
    virtual double getAverageNotional1(
        ApplicationWarning& warning,
        const Date& startDate,
        const Date& endDate = Date()
        ) const
    {
        Date eDate = endDate.isEmpty() ? getFixedEndDate(startDate) : endDate;
        return AnalyticCapitalInstrumentsHelper::getAverageNotional(
            warning, fixedNotional_m, fixedPeriods_m, "fixed", startDate, eDate);
    }

    // Average notional for a float leg of a single ccy swap.
    virtual double getAverageNotional2(
        ApplicationWarning& warning,
        const Date& startDate,
        const Date& endDate = Date()
        ) const
    {
        Date eDate = endDate.isEmpty() ? getFloatEndDate(startDate) : endDate;
        return AnalyticCapitalInstrumentsHelper::getAverageNotional(
            warning, floatNotional_m, floatPeriods_m, "float", startDate, eDate);
    }
#endif

    virtual void getStartAndEndDates(
        const Date& nowDate,
        Date& start,
        Date& end) const
    {
        getStartAndEndDatesForFixedFloatSwap(nowDate, start, end);
    }

#ifndef RISK_MCVAR_DISABLE
    // For time-dependent IR Swap instruments
    virtual AnalyticCapitalTradeBase::Ptr getAnalyticCapitalInfoSingleCcy(
        const Date& valueDate,
        double marketValue,
        bool position,
        const string& id,
        ApplicationWarning& warning
        )
    {
        string tradeType = AnalyticCapitalTradeBase::tradeRegularStr();

        double notional = getAverageNotional1(warning, valueDate);
        string ccy = getCurrency().toString();

        Date startDate, endDate;
        getStartAndEndDates(valueDate, startDate, endDate);

        return Shared_ptr<AnalyticCapitalTradeIR>(new AnalyticCapitalTradeIR(
            ccy,
            endDate,
            marketValue,
            notional,
            position,
            startDate,
            endDate,
            tradeType,
            id)
            );
    }

    // For time-dependent IR CC Swap instruments (treated as FX)
    virtual AnalyticCapitalTradeBase::Ptr getAnalyticCapitalInfoCrossCcy(
        const Date& valueDate,
        double marketValue,
        bool position,
        const string& id,
        ApplicationWarning& warning
        )
    {
        string tradeType = AnalyticCapitalTradeBase::tradeRegularStr();

        double notional1 = getAverageNotional1(warning, valueDate);
        string ccy1 = getFixedCurrency().toString();

        double notional2 = getAverageNotional2(warning, valueDate);
        string ccy2 = getFloatCurrency().toString();

        Date startDate, endDate;
        getStartAndEndDates(valueDate, startDate, endDate);

        return Shared_ptr<AnalyticCapitalTradeFX>(new AnalyticCapitalTradeFX(
            ccy1,
            endDate,
            marketValue,
            notional1,
            position,
            notional2,
            ccy2,
            tradeType,
            id)
            );
    }
#endif

    virtual bool hasYCProducts() const
    {
        return false;
    }

    virtual YCProductInstrument::Ptr getAbstractProduct(bool isCC,
                                                        const YCProductFactory& factory,
                                                        const Date& nowDate,
                                                        const Date& spotDate,
                                                        double fixedScale,
                                                        double floatScale,
                                                        const Currency& curveCurrency,
                                                        const IQuotes::CPtr& quotes,
                                                        const Bump::CPtr& bump,
                                                        int& bumpUsedOut,
                                                        bool fudgeFirstFixing,
                                                        ApplicationWarning& warning) const
    {
        warning.throwFatal("getAbstractProduct is not supported for swaps with time-dependent parameters");
        return YCProductInstrument::Ptr();
    }

    virtual YCProductInstrument::Ptr getDiscYCProduct(const Currency& domCurrency,
                                                      const Currency& forCurrency,
                                                      const YieldCurve_I::CPtr& forDisc,
                                                      const YieldCurve_I::CPtr& forProj,
                                                      const YieldCurve_I::CPtr& domProj,
                                                      const Date& nowDate,
                                                      const Date& spotDate,
                                                      double fixedScale,
                                                      double floatScale,
                                                      const Currency& curveCurrency,
                                                      const IQuotes::CPtr& quotes,
                                                      const Bump::CPtr& bump,
                                                      int& bumpUsedOut,
                                                      bool fudgeFirstFixing,
                                                      ApplicationWarning& warning,
                                                      IInstrumentStripCC::YCUsed& ycUsed) const
    {
        warning.throwFatal("getDiscYCProduct is not supported for swaps with time-dependent parameters");
        return YCProductInstrument::Ptr();
    }

    virtual YCProductInstrument::Ptr getProjYCProduct(const YieldCurve_I::CPtr& domDisc,
                                                      const Currency& domCurrency,
                                                      const YieldCurve_I::CPtr& forDisc,
                                                      const Currency& forCurrency,
                                                      const Date& nowDate,
                                                      const Date& spotDate,
                                                      double fixedScale,
                                                      double floatScale,
                                                      const Currency& curveCurrency,
                                                      const IQuotes::CPtr& quotes,
                                                      const Bump::CPtr& bump,
                                                      int& bumpUsedOut,
                                                      bool fudgeFirstFixing,
                                                      ApplicationWarning& warning,
                                                      IInstrumentStripCC::YCUsed &ycUsed) const
    {
        warning.throwFatal("getProjYCProduct is not supported for swaps with time-dependent parameters");
        return YCProductInstrument::Ptr();
    }

    virtual YCProductInstrument::Ptr getYCProduct_impl(const Date& nowDate,
                                                       const IQuotes::CPtr& quotes,
                                                       const Bump::CPtr& bump,
                                                       int& bumpUsedOut,
                                                       bool fudgeFirstFixing,
                                                       const InstrumentYield_I::FXInfo& fxInfo,
                                                       ApplicationWarning& warning) const
    {
        warning.throwFatal("getYCProduct_impl is not supported for swaps with time-dependent parameters");
        return YCProductInstrument::Ptr();
    }

    virtual YCProductInstrument::Ptr getProjectionProduct(const YieldCurve_I::CPtr& discountCurve,
                                                          const Date& nowDate,
                                                          const IQuotes::CPtr& quotes,
                                                          const Bump::CPtr& bump,
                                                          int& bumpUsedOut,
                                                          ApplicationWarning& warning) const
    {
        warning.throwFatal("getProjectionProduct is not supported for swaps with time-dependent parameters");
        return YCProductInstrument::Ptr();
    }

    virtual FIN_SwapInstrument::Ptr instrument(const Date& nowDate,
                                               const IQuotes::CPtr quotes,
                                               const BumpShift* bump,
                                               bool unitNotional,
                                               const CurveTenorInterpolated::Ptr& floatStubIndexCurve,
                                               const Currency& curveCurrency,
                                               int& bumpUsed,
                                               ApplicationWarning& warning,
                                               Date& startOut,
                                               Date& endOut,
                                               bool fudgeFirstFixing) const
    {
        if(unitNotional)
            warning.throwFatal("FIN_SwapInstrument with unitNotional = true is not supported for swap with time-dependent parameters");

        ScheduleInfo::Ptr schedule = calc_schedule(nowDate, fudgeFirstFixing);

        if(schedule->fixed_m.dates_m.empty())
            throwAppException("[" + getID() + "] empty dates");

        startOut = schedule->fixed_m.dates_m.front()->getPeriodStartDate();

        FIN_PaymentStream::Ptr fixedLeg(NULL);
        if(fixedRate_m.size())
        {
            vector<double> fixedRates(fixedRate_m.size());
            for(size_t i=0; i<fixedRates.size(); ++i)
                fixedRates[i] = fixedRate_m[i]->getValue(quotes, bump, bumpUsed);

            fixedLeg = schedule->fixed_m.makeStream(fixedNotional_m,
                                                    fixedRates,
                                                    compounding_frequency_m);
        }
        else if(fixedCashflows_m.size())
        {
            fixedLeg = schedule->fixed_m.makeStream(fixedNotional_m,
                                                    fixedCashflows_m);
        }
        else
        {
            warning.throwFatal("Fixed rates or fixed cashflows must be supplied");
        }

        vector<double> floatRates(floatRate_m.size());
        for(size_t i=0; i<floatRates.size(); ++i)
            floatRates[i] = floatRate_m[i]->getValue(quotes, bump, bumpUsed);

        IFloatStream::Ptr
            floatLeg(schedule->float_m.makeStream(floatNotional_m,
                                                  floatRates,
                                                  queryFixingsFunc()));

        FIN_SwapInstrument::Ptr ret;
        ret = new FIN_SwapInstrument(fixedLeg,
                                     floatLeg);
        endOut = ret->getLastDate();
        return ret;
    }

    virtual CrossCurrencyBasisSwapInstrument::CPtr ccInstrument(const Date& now_date,
                                                                const pair<bool,bool>& notionalExchange,
                                                                const IQuotes::CPtr quotes,
                                                                const BumpShift* bump,
                                                                bool unitNotional,
                                                                const CurveTenorInterpolated::Ptr& floatStubIndexCurve,
                                                                const Currency& curveCurrency,
                                                                int& bump_used_out,
                                                                ApplicationWarning& warn,
                                                                Date& start_out,
                                                                Date& end_out,
                                                                bool fudge_first_fixing) const
    {
        FIN_SwapInstrument::Ptr swap = instrument(now_date,
                                                  quotes, bump,
                                                  unitNotional,
                                                  floatStubIndexCurve,
                                                  curveCurrency,
                                                  bump_used_out,
                                                  warn,
                                                  start_out, end_out,
                                                  fudge_first_fixing);

        NotionalExchange notExObj = NotionalExchange(notionalExchange.first, notionalExchange.second, false);
        return mkSharedPtr(new CrossCurrencyBasisSwapInstrument(swap,
                                                                notExObj,
                                                                fixedNotionalPayDates_m,
                                                                floatNotionalPayDates_m));
    }

    virtual void getQuotes(QuoteSet& quotesOut) const
    {
        for(size_t i=0; i<fixedRate_m.size(); ++i)
            fixedRate_m[i]->getQuotes(quotesOut);

        for(size_t i=0; i<floatRate_m.size(); ++i)
            floatRate_m[i]->getQuotes(quotesOut);
    }

    virtual InstrumentQuote::NameCRef rateQuote()  const
    {
        throwFatalException("rateQuote is not supported for swaps with time-dependent parameters");
        return fixedRate_m[0]->getName();
    }

    virtual InstrumentQuote::NameCRef priceQuote() const
    {
        throwFatalException("priceQuote is not supported for swaps with time-dependent parameters");
        return empty_string_s;
    }

    virtual InstrumentQuote::NameCRef volQuote()   const
    {
        throwFatalException("volQuote is not supported for swaps with time-dependent parameters");
        return empty_string_s;
    }

    virtual double getRate(const IQuotes::CPtr quotes,
                           const BumpShift* bump,
                           int& bumpUsedOut,
                           ApplicationWarning& warning) const
    {
        warning.throwFatal("getRate is not supported for swaps with time-dependent parameters");
        return 0.0;
    }

    virtual double getPrice(const IQuotes::CPtr quotes,
                            const BumpShift* bump,
                            int& bumpUsedOut,
                            ApplicationWarning& warning) const
    {
        warning.throwFatal("getPrice is not supported for swaps with time-dependent parameters");
        return 0.0;
    }

    virtual double getVol(const IQuotes::CPtr quotes,
                          const BumpShift* bump,
                          int& bumpUsedOut,
                          ApplicationWarning& warning) const
    {
        warning.throwFatal("getPrice is not supported for swaps with time-dependent parameters");
        return 0.0;
    }

    virtual const Currency& getPayoutCurrency() const
    {
        return *payoutCurrency_m;
    }

    virtual CurveYieldBase::Ptr getFxProjectionCurve() const
    {
        return fxProjectionCurve_m;
    }

    virtual CurveYieldBase::Ptr getFxProjectionPayoutCurve() const
    {
        return fxProjectionPayoutCurve_m;
    }

    virtual vector<Date> getFixedFxFixingDates() const
    {
        return fixedFxFixingDates_m;
    }

    virtual vector<Date> getFloatFxFixingDates() const
    {
        return floatFxFixingDates_m;
    }

    virtual pair<Date,Date> getFixedNotionalFxFixingDates() const
    {
        return fixedNotionalFxFixingDates_m;
    }

    virtual pair<Date,Date> getFloatNotionalFxFixingDates() const
    {
        return floatNotionalFxFixingDates_m;
    }

    virtual DateFunction_I::CPtr getFxFixings() const
    {
        return fxFixings_m;
    }

    virtual pair<bool,bool> getNotionalExchange() const
    {
        return pair<bool,bool>(initialNotionalExchange_m, finalNotionalExchange_m);
    }

protected:
    virtual bool validate()
    {
        if(!Swap::validate())
            return false;

        hasTwoLegs_m = true;

        const ParseResult& result = getParameters();
        ApplicationWarning& warning = getWarnings();

        parseUseRateDates(result, warning);
        parseCompoundingFrequency(result, tkFixedCompoundingType);

        parseEventsRelatedHeaders(result, warning);

        fixedBasis_m = Basis_I::createBasis(basisType_m);
        floatBasis_m = Basis_I::createBasis(floatBasisType_m);

        parseEvents(result, tkInstrumentFixedEvents, basisType_m, fixedPeriods_m, fixedBasis_m, warning);
        parseEvents(result, tkFloatEvents, floatBasisType_m, floatPeriods_m, floatBasis_m, warning);

        RegisteredObject::Ptr floatObj = result.getPtr(tkFloatEvents);
        EventsInterestBase::Ptr floatEvents = dyn_cast<EventsInterestBase>(floatObj);
        if(floatEvents)
            setMinorWarnings(floatEvents,
                             warning);

        parseNotionalTable(result, tkFixedNotionalTable, fixedPeriods_m, fixedNotional_m, warning);
        parseNotionalTable(result, tkFloatNotionalTable, floatPeriods_m, floatNotional_m, warning);

        fixedRate_m.resize(0);
        fixedCashflows_m.resize(0);

        const bool hasFixedRate = parseRateTable(result, tkFixedRateTable, fixedPeriods_m, fixedRate_m, warning);

        ParseResult::TablePtr fixedCashflowTable = result.queryTable(tkFixedCashflowTable);
        if(fixedCashflowTable)
        {
            if(hasFixedRate)
                warning.setMinor("fixed rate and fixed cashflows are both supplied; only the cashflows are used");

            const vector<double>& theCashflows = fixedCashflowTable->getDoubleColumn(tkCashflows);
            if(theCashflows.size() != fixedPeriods_m.size())
               warning.throwFatal("size of " + tkFixedCashflowTable.toString() + " vector and schedule table do not match");
            fixedCashflows_m = theCashflows;
        }
        else if(!hasFixedRate)
        {
            warning.throwFatal("A fixed rate table or a fixed cashflow table must be supplied");
        }

        parseSpreadTable(result, tkFloatSpreadTable, floatPeriods_m, floatRate_m, warning);

        hasFxFixingDates_m = false;

        fixedFxFixingDates_m.resize(fixedPeriods_m.size());
        for(size_t i=0; i<fixedFxFixingDates_m.size(); ++i)
        {
            fixedFxFixingDates_m[i] = fixedPeriods_m[i]->getPaymentDate();
        }
        floatFxFixingDates_m.resize(floatPeriods_m.size());
        for(size_t i=0; i<floatFxFixingDates_m.size(); ++i)
        {
            floatFxFixingDates_m[i] = floatPeriods_m[i]->getPaymentDate();
        }

        initialNotionalExchange_m = result.getBool(tkInitialNotionalExchange, true);
        finalNotionalExchange_m = result.getBool(tkFinalNotionalExchange, true);

        const Date initialNotionalExchangeDate = result.queryDate(tkInitialNotionalExchangeDate);
        const Date finalNotionalExchangeDate = result.queryDate(tkFinalNotionalExchangeDate);

        fixedNotionalFxFixingDates_m.first =
        		(fixedPeriods_m.empty() || initialNotionalExchangeDate.isValid()) ? initialNotionalExchangeDate : fixedPeriods_m.front()->getPeriodStartDate();
        fixedNotionalFxFixingDates_m.second =
        		(fixedPeriods_m.empty() || finalNotionalExchangeDate.isValid()) ? finalNotionalExchangeDate : fixedPeriods_m.back()->getPaymentDate();
        fixedNotionalPayDates_m = fixedNotionalFxFixingDates_m;

        floatNotionalFxFixingDates_m.first =
        		(floatPeriods_m.empty() || initialNotionalExchangeDate.isValid()) ? initialNotionalExchangeDate : floatPeriods_m.front()->getPeriodStartDate();
        floatNotionalFxFixingDates_m.second =
        		(floatPeriods_m.empty() || finalNotionalExchangeDate.isValid()) ? finalNotionalExchangeDate : floatPeriods_m.back()->getPaymentDate();
        floatNotionalPayDates_m = floatNotionalFxFixingDates_m;

        CurveFixings::CPtr fxFixings = result.queryPtr(tkFXFixings);
        if(fxFixings)
            fxFixings_m = fxFixings->createDateFunction();

        floatStubIndexCurve_m = result.queryPtr(tkFloatStubIndexCurve);

        const Date temp;
        schedule_m = calc_schedule(temp, false);

        return true;
    }

    class TimeDependentSwapPayoff : public Swap::SwapPayoff
    {
    public:
        TimeDependentSwapPayoff(const CurrencyType fixedCurrType,
                                const CurrencyType floatCurrType,
                                bool useCashflows,
                                bool initialNotionalExchange,
                                bool finalNotionalExchange,
                                bool doFrontStubInterpolation,
                                bool doBackStubInterpolation,
                                const pair<double,double>& frontStubRateWeights,
                                const pair<double,double>& backStubRateWeights) :
        SwapPayoff(fixedCurrType,
                   floatCurrType,
                   doFrontStubInterpolation,
                   doBackStubInterpolation,
                   frontStubRateWeights,
                   backStubRateWeights),
        useCashflows_m(useCashflows),
        initialNotionalExchange_m(initialNotionalExchange),
        finalNotionalExchange_m(finalNotionalExchange)
        {}

        virtual const PricingDirectionType getPricingDirection() const
        {
            return DirectionEither;
        }

        virtual void doPayoff()
        {
            Underlying fixed_leg = get(coupon_leg_str);
            Underlying float_leg = get(fund_leg_str);

            if(isActive(coupon_date_str))
            {
                if(!useCashflows_m)
                {
                    const Underlying fixedNotional = get(fixed_notional);
                    doFixedLeg(fixedNotional,
                               fixed_leg);
                }
                else
                {
                    doFixedLeg(fixed_leg);
                }
            }

            if(isActive(fund_date_str))
            {
                const Underlying floatNotional = get(float_notional);
                doFloatLeg(floatNotional,
                           float_leg);
            }

            if(isCrossCurrency_m)
            {
                const Underlying fixedNotional = get(fixed_notional);
                const Underlying floatNotional = get(float_notional);
                doNotionalExchange(fixedNotional, floatNotional,
                                   initialNotionalExchange_m, finalNotionalExchange_m,
                                   fixed_leg, float_leg);
            }

            Underlying swap = get(swap_str);
            getSwap(fixed_leg, float_leg,
                    swap);
        }

        virtual Cloneable_I* clone() const
        {
            return new TimeDependentSwapPayoff(fixedCurrType_m,
                                               floatCurrType_m,
                                               useCashflows_m,
                                               initialNotionalExchange_m,
                                               finalNotionalExchange_m,
                                               doFrontStubInterpolation_m,
                                               doBackStubInterpolation_m,
                                               frontStubRateWeights_m,
                                               backStubRateWeights_m);
        }

    private:
        bool useCashflows_m;
        bool initialNotionalExchange_m;
        bool finalNotionalExchange_m;
    };

    virtual void registerData(const NXKernel_I::Ptr& kernel,
                              const Bump* bump,
                              int& bumpUsedOut)
    {
        const BumpShift* bumpShift = dynamic_cast<const BumpShift*>(bump);

        if(fixedRate_m.size())
        {
            vector<double> fixedRates(fixedRate_m.size());
            for(size_t i=0; i<fixedRates.size(); ++i)
                fixedRates[i] = fixedRate_m[i]->getValue(bumpShift, bumpUsedOut);
            kernel->registerData(fixed_str, schedule_m->fixedFixingDates(), fixedRates, InterpolationFlatLHS);
        }
        else
        {
            kernel->registerData(fixed_cashflow_str, schedule_m->fixedFixingDates(), fixedCashflows_m, InterpolationFlatLHS);
        }
        kernel->registerData(fixed_notional, schedule_m->fixedFixingDates(), fixedNotional_m, InterpolationFlatLHS);

        vector<double> floatRates(floatRate_m.size());
        for(size_t i=0; i<floatRates.size(); ++i)
            floatRates[i] = floatRate_m[i]->getValue(bumpShift, bumpUsedOut);
        kernel->registerData(float_str, schedule_m->floatFixingDates(), floatRates, InterpolationFlatLHS);
        kernel->registerData(float_notional, schedule_m->floatFixingDates(), floatNotional_m, InterpolationFlatLHS);
    }

    virtual void registerPayoff(const NXKernel_I::Ptr& kernel,
                                CurrencyType fixedCurrencyType,
                                CurrencyType floatCurrencyType,
                                bool doFrontStubInterpolation,
                                bool doBackStubInterpolation,
                                const pair<double,double>& frontStubRateWeights,
                                const pair<double,double>& backStubRateWeights)
    {
        TimeDependentSwapPayoff payoff(fixedCurrencyType,
                                       floatCurrencyType,
                                       fixedRate_m.size() ? false : true,
                                       initialNotionalExchange_m,
                                       finalNotionalExchange_m,
                                       doFrontStubInterpolation,
                                       doBackStubInterpolation,
                                       frontStubRateWeights,
                                       backStubRateWeights);

        kernel->registerPayoff(payoff);
    }

    virtual ScheduleInfo::Ptr calc_schedule(const Date& nowDate,
                                            bool fudge_first_fixing) const
    {
        return ScheduleInfo::Ptr(new ScheduleInfo(fixedPeriods_m,
                                                  fixedBasis_m,
                                                  floatPeriods_m,
                                                  floatBasis_m,
                                                  rateBasis_m,
                                                  useRateDates_m,
                                                  rateAccrualCal_m,
                                                  rateFrequency_m,
                                                  rateFixingCal_m,
                                                  rateSpotLag_m,
                                                  rateEndOfMonth_m));
    }

protected:

    virtual void view(ApplicationData& out) const {
        viewWithSchedule(out, schedule_m); 
    }

	ScheduleInfo::Ptr schedule_m;

    const Currency* payoutCurrency_m;
    CurveYieldBase::Ptr fxProjectionCurve_m;
    CurveYieldBase::Ptr fxProjectionPayoutCurve_m;
    vector<Date> fixedFxFixingDates_m;
    vector<Date> floatFxFixingDates_m;
    DateFunction_I::CPtr fxFixings_m;
    bool hasFxFixingDates_m;

    bool initialNotionalExchange_m;
    bool finalNotionalExchange_m;

    pair<Date,Date> fixedNotionalFxFixingDates_m;
    pair<Date,Date> fixedNotionalPayDates_m;
    pair<Date,Date> floatNotionalFxFixingDates_m;
    pair<Date,Date> floatNotionalPayDates_m;

private:
    vector<double> fixedNotional_m;
    vector<double> floatNotional_m;
    vector<InstrumentQuote::Ptr> fixedRate_m;
    vector<double> fixedCashflows_m;
    vector<InstrumentQuote::Ptr> floatRate_m;
};

class TimeDependentSwapSingleCcy : public TimeDependentSwap
{
public:
    struct Check : public TimeDependentSwap::Check
    {
        typedef TimeDependentSwapSingleCcy ObjectType;
        Check() :
            TimeDependentSwap::Check(irswap_s, tkIRSwap, "IR Swap from custom event schedules", 20)
        {
            addRequired(tkCurrency, ParseString);
        }
    };

#ifndef RISK_MCVAR_DISABLE
    virtual AnalyticCapitalTradeBase::Ptr getAnalyticCapitalInfo(
        const Date& valueDate,
        double marketValue,
        bool position,
        const string& id,
        const DataInput::CPtr& instrInput,
        ApplicationWarning& warning
        )
    {
        return getAnalyticCapitalInfoSingleCcy(valueDate, marketValue, position, id, warning);
    }
#endif

protected:
    virtual bool validate()
    {
        if(!TimeDependentSwap::validate())
            return false;

        payoutCurrency_m = &getCurrency();

        return true;
    }

    virtual void doRegistration(const NXKernel_I::Ptr& kernel,
                                Model_I::PtrCRef model,
                                const Bump* bump,
                                ApplicationWarning& warning,
                                int& bumpUsedOut)
    {
        doRegistrationTwoLegs(kernel, model, bump, warning, bumpUsedOut,
                              schedule_m->fixed_m.basis_m->getBasisType(),
                              schedule_m->float_m.floatBasis_m->getBasisType(),
                              schedule_m->float_m.rateBasis_m->getBasisType(),
                              schedule_m ? &schedule_m->float_m.freq_m : NULL,
                              schedule_m->float_m.acc_cal_m.get(),
                              &schedule_m->float_m.fix_lag_m,
                              schedule_m->float_m.fixing_cal_m.get());
    }

    virtual bool hasMaturityTenor() const { return false; }
};

class TimeDependentNonDeliverableSwapSingleCcy : public TimeDependentSwap
{
public:
    struct Check : public TimeDependentSwap::Check
    {
        typedef TimeDependentNonDeliverableSwapSingleCcy ObjectType;
        Check() :
            TimeDependentSwap::Check(irswap_s, tkIRSwap, "Non-deliverable IR Swap from custom event schedules", 20)
        {
            addRequired(tkCurrency, ParseString);

            addOptional(tkPayoutCurrency, ParseString);
            addOptional(tkFxProjectionCurrencyCurve, ParseID);
            addOptional(tkFxProjectionPayoutCurrencyCurve, ParseID);
            addOptional(tkFXFixings, ParseString);
            addOptional(tkFixedFxFixingDateTable, ParseID);
            addOptional(tkFloatFxFixingDateTable, ParseID);
        }
    };

#ifndef RISK_MCVAR_DISABLE
    virtual AnalyticCapitalTradeBase::Ptr getAnalyticCapitalInfo(
        const Date& valueDate,
        double marketValue,
        bool position,
        const string& id,
        const DataInput::CPtr& instrInput,
        ApplicationWarning& warning
        )
    {
        return getAnalyticCapitalInfoSingleCcy(valueDate, marketValue, position, id, warning);
    }
#endif

protected:
    virtual bool validate()
    {
        if(!TimeDependentSwap::validate())
            return false;

        const ParseResult& result = getParameters();
        ApplicationWarning& warning = getWarnings();

        if(result.queryCurrency(tkPayoutCurrency) != Currency::Failed)
        {
            payoutCurrency_m = &result.getCurrency(tkPayoutCurrency);
        }
        else
        {
            payoutCurrency_m = &getCurrency();
        }

        fxProjectionCurve_m = result.queryPtr(tkFxProjectionCurrencyCurve);
        fxProjectionPayoutCurve_m = result.queryPtr(tkFxProjectionPayoutCurrencyCurve);

        parseFxFixingDateTable(result,
                               tkFixedFxFixingDateTable,
                               schedule_m->fixed_m.dates_m,
                               hasFxFixingDates_m,
                               fixedFxFixingDates_m,
                               warning);
        parseFxFixingDateTable(result,
                               tkFloatFxFixingDateTable,
                               schedule_m->float_m.dates_m,
                               hasFxFixingDates_m,
                               floatFxFixingDates_m,
                               warning);

        parseNotionalExchangeTable(result,
                                   tkNotionalExchangeDateTable,
                                   initialNotionalExchange_m,
                                   finalNotionalExchange_m,
                                   schedule_m->fixed_m.dates_m,
                                   schedule_m->float_m.dates_m,
                                   fixedNotionalFxFixingDates_m,
                                   fixedNotionalPayDates_m,
                                   floatNotionalFxFixingDates_m,
                                   floatNotionalPayDates_m,
                                   warning);

        return true;
    }

    virtual void doRegistration(const NXKernel_I::Ptr& kernel,
                                Model_I::PtrCRef model,
                                const Bump* bump,
                                ApplicationWarning& warning,
                                int& bumpUsedOut)
    {
        warning.throwFatal("Non-deliverable swaps are not supported by Kernel pricing");
    }

    virtual bool isNonDeliverable() const
    {
        return *payoutCurrency_m != getCurrency();
    }

    virtual bool hasMaturityTenor() const { return false; }

};

class TimeDependentSwapCrossCcy : public TimeDependentSwap, public TwoCurrencies
{
public:
    struct Check : public TimeDependentSwap::Check
    {
        typedef TimeDependentSwapCrossCcy ObjectType;
        Check() :
            TimeDependentSwap::Check(ccirswap_s, tkCCIRSwap, "IR Cross-currency Swap from custom event schedules", 20)
        {
            addRequired(fixed_leg_conv_spec_m, tkFixedCurrency, ParseString, tkCurrency);
            addRequired(float_leg_conv_spec_m, tkFloatCurrency, ParseString, tkCurrency);

            addOptional(tkInitialNotionalExchange, ParseBool);
            addOptional(tkFinalNotionalExchange, ParseBool);
            addOptional(tkInitialNotionalExchangeDate, ParseString | ParseDate);
            addOptional(tkFinalNotionalExchangeDate, ParseString | ParseDate);
        }
    };

    const Currency& getFixedCurrency() const
    {
        return *fixed_m;
    }

    const Currency& getFloatCurrency() const
    {
        return *float_m;
    }

    virtual bool currencyIsRequired() const
    {
        return false;
    }

#ifndef RISK_MCVAR_DISABLE
    virtual AnalyticCapitalTradeBase::Ptr getAnalyticCapitalInfo(
        const Date& valueDate,
        double marketValue,
        bool position,
        const string& id,
        const DataInput::CPtr& instrInput,
        ApplicationWarning& warning
        )
    {
        return getAnalyticCapitalInfoCrossCcy(valueDate, marketValue, position, id, warning);
    }
#endif

protected:
    virtual bool validate()
    {
        if(!TimeDependentSwap::validate())
            return false;

        const ParseResult& result = getParameters();

        parseCurrencies(result);

        payoutCurrency_m = &getFixedCurrency();

        return true;
    }

    virtual void doRegistration(const NXKernel_I::Ptr& kernel,
                                Model_I::PtrCRef model,
                                const Bump* bump,
                                ApplicationWarning& warning,
                                int& bumpUsedOut)
    {
        doRegistrationTwoLegs(kernel, model, bump, warning, bumpUsedOut,
                              schedule_m->fixed_m.basis_m->getBasisType(),
                              schedule_m->float_m.floatBasis_m->getBasisType(),
                              schedule_m->float_m.rateBasis_m->getBasisType(),
                              useRateDates_m ? &schedule_m->float_m.freq_m : NULL,
                              schedule_m->float_m.acc_cal_m.get(),
                              &schedule_m->float_m.fix_lag_m,
                              schedule_m->float_m.fixing_cal_m.get());
    }

    virtual bool hasMaturityTenor() const { return false; }
};

class TimeDependentNonDeliverableSwapCrossCcy : public TimeDependentSwap, public TwoCurrencies
{
public:
    struct Check : public TimeDependentSwap::Check
    {
        typedef TimeDependentNonDeliverableSwapCrossCcy ObjectType;
        Check() :
            TimeDependentSwap::Check(ccirswap_s, tkCCIRSwap, "Non-deliverable IR Cross-currency Swap from custom event schedules", 20)
        {
            addRequired(fixed_leg_conv_spec_m, tkFixedCurrency, ParseString, tkCurrency);
            addRequired(float_leg_conv_spec_m, tkFloatCurrency, ParseString, tkCurrency);
            addRequired(tkPayoutCurrency, ParseString);

            addOptional(tkFixedFxProjectionCurve, ParseID);
            addOptional(tkFloatFxProjectionCurve, ParseID);
            addOptional(tkFXFixings, ParseString);
            addOptional(tkFxFixingDateTable, ParseID);
            addOptional(tkNotionalExchangeDateTable, ParseID);

            addOptional(tkInitialNotionalExchange, ParseBool);
            addOptional(tkFinalNotionalExchange, ParseBool);
        }
    };

    const Currency& getFixedCurrency() const
    {
        return *fixed_m;
    }

    const Currency& getFloatCurrency() const
    {
        return *float_m;
    }

    virtual bool currencyIsRequired() const
    {
        return false;
    }

#ifndef RISK_MCVAR_DISABLE
    virtual AnalyticCapitalTradeBase::Ptr getAnalyticCapitalInfo(
        const Date& valueDate,
        double marketValue,
        bool position,
        const string& id,
        const DataInput::CPtr& instrInput,
        ApplicationWarning& warning
        )
    {
        return getAnalyticCapitalInfoCrossCcy(valueDate, marketValue, position, id, warning);
    }
#endif

protected:
    virtual bool validate()
    {
        if(!TimeDependentSwap::validate())
            return false;

        const ParseResult& result = getParameters();
        ApplicationWarning& warning = getWarnings();

        parseCurrencies(result);

        payoutCurrency_m = &result.getCurrency(tkPayoutCurrency);

        if(*payoutCurrency_m == getFixedCurrency())
        {
            parseFxFixingDateTable(result,
                                   tkFxFixingDateTable,
                                   schedule_m->float_m.dates_m,
                                   hasFxFixingDates_m,
                                   floatFxFixingDates_m,
                                   warning);
        }
        else if(*payoutCurrency_m == getFloatCurrency())
        {
            parseFxFixingDateTable(result,
                                   tkFxFixingDateTable,
                                   schedule_m->fixed_m.dates_m,
                                   hasFxFixingDates_m,
                                   fixedFxFixingDates_m,
                                   warning);
        }
        else
        {
            warning.throwFatal("Payout currency must be either the fixed currency or the float currency");
        }

        parseNotionalExchangeTable(result,
                                   tkNotionalExchangeDateTable,
                                   initialNotionalExchange_m,
                                   finalNotionalExchange_m,
                                   schedule_m->fixed_m.dates_m,
                                   schedule_m->float_m.dates_m,
                                   fixedNotionalFxFixingDates_m,
                                   fixedNotionalPayDates_m,
                                   floatNotionalFxFixingDates_m,
                                   floatNotionalPayDates_m,
                                   warning);

        fxProjectionCurve_m = result.queryPtr(tkFloatFxProjectionCurve); // projection is float
        fxProjectionPayoutCurve_m = result.queryPtr(tkFixedFxProjectionCurve); // projection payout is fixed

        return true;
    }

    virtual void doRegistration(const NXKernel_I::Ptr& kernel,
                                Model_I::PtrCRef model,
                                const Bump* bump,
                                ApplicationWarning& warning,
                                int& bumpUsedOut)
    {
        warning.throwFatal("Non-deliverable swaps are not supported by Kernel pricing");
    }

    virtual bool isNonDeliverable() const
    {
        return fxProjectionCurve_m.get() || fxProjectionPayoutCurve_m.get() || hasFxFixingDates_m;
    }

    virtual bool hasMaturityTenor() const { return false; }
};

// ======================================================================
// The following code is deprecated
// ======================================================================

struct SingleLegCheck : public LegCheck
{
    SingleLegCheck()
    {
        tk_basis = tkCommonBasis;
        tk_freq = tkCommonFrequency;
        tk_acc_cal = tkEventsAccrualCalendar;
        tk_acc_conv = tkEventsAccrualConvention;
        tk_pay_cal = tkEventsPayCalendar;
        tk_pay_conv = tkEventsPayConvention;
        tk_pay_lag = tkEventsPayLag;
        tk_front_stub = tkEventsFrontStub;
        tk_long_stub = tkEventsLongStub;
    }
};

const FixedLegCheck fixed_leg_check_s;
const FloatLegCheck float_leg_check_s;
const SingleLegCheck single_leg_check_s;

struct DateRange
{
    Date start_date_m, end_date_m;

    void init(const ParseResult& result)
    {
        start_date_m = result.getDate(tkEventsStartDate);
        end_date_m = result.getDate(tkEventsEndDate);
    }

    static void addChecks(ParseCheck & check)
    {
        check.addRequired(tkEventsStartDate, ParseDate);
        check.addRequired(tkEventsEndDate, ParseDate);
    }
};

struct TenorRange
{
    Tenor start_tenor_m, end_tenor_m;
    Date ref_date_m;

    void init(const ParseResult& result)
    {
        start_tenor_m = result.getTenor(tkEventsStartTenor, zero_bd_s);
        end_tenor_m = result.getTenor(tkEventsEndTenor);
        ref_date_m = result.queryDate(tkEventsRefDate);
    }

    static void addChecks(ParseCheck& check, ConventionSpec& spec)
    {
        check.addOptional(tkEventsRefDate, ParseDate);
        check.addOptional(spec,tkEventsStartTenor, ParseString, tkEventsStartTenor);
        check.addRequired(spec,tkEventsEndTenor, ParseString, tkEventsEndTenor);
    }
};

struct FixingInfo
{
    Tenor fixing_lag_m;
    Calendar_I::Ptr fixing_cal_m;

    void init(const ParseResult& result)
    {
        string fix_cal = result.getString(tkEventsFixCalendar, "NONE");
        fixing_cal_m = result.getCalendar("F", fix_cal);
        fixing_lag_m = result.getTenor(tkEventsFixingLag, zero_bd_s);
    }

    static void addChecks(ParseCheck& check, ConventionSpec& spec)
    {
        check.addOptional(spec, tkEventsFixCalendar, ParseString, tkEventsFixCalendar);
        check.addOptional(spec, tkEventsFixingLag, ParseString, tkEventsFixingLag);
    }

    void getBoundaries(
        Date& start,
        Date& end,
        const Date& now_date,
        const Calendar_I* acc_cal,
        const TenorRange& tenorRange,
        const Calendar_I* nominal_cal) const
    {
        start = now_date;
        fixing_cal_m->addTenor(start, fixing_lag_m);
        if (!acc_cal)
            acc_cal = fixing_cal_m.get();
        acc_cal->addTenor(start, tenorRange.start_tenor_m);
        acc_cal->addTenor(start, Tenor("0bd"));

        end = start;
        nominal_cal->addTenor(end, tenorRange.end_tenor_m);
    }
};

struct HiddenFloatBasis
{
    static void init(const ParseResult& result)
    {
        if (result.querySingle(tkFloatBasis) && result.getObject())
            result.getObject()->getWarnings().setMinor("Float Basis is not used in a fixed leg Schedule");
    }

    static void addChecks(ParseCheck& check)
    {
        check.addHidden(tkFloatBasis, ParseString);
    }
};

class OldSwap : public ConstantParameterSwap
{
public:
    struct Check : public ConstantParameterSwap::Check
    {
        Check(const char* msg, int priority) :
            ConstantParameterSwap::Check(irswap_s, tkIRSwap, msg, priority)
        {
            addRequired(tkRateDividend, ParseDouble);
            addOptional(tkCompoundingType, ParseString);

            addRequired(tkCurrency, ParseString);
            addOptional(tkStructureNotional, ParseDouble);
        }
    };

protected:
    virtual bool validate()
    {
        if(!ConstantParameterSwap::validate())
            return false;

        const ParseResult& result = getParameters();
        parseSingleCcyNotional(result);
        parseRates(result, tkRateDividend);
        parseCompoundingFrequency(result, tkCompoundingType);

        return true;
    }

    void doRegistrationOneLeg(const NXKernel_I::Ptr& kernel,
                              Model_I::PtrCRef model,
                              const Bump* bump,
                              ApplicationWarning& warning,
                              int& bumpUsedOut,
                              BasisType basisType)
    {
        CurrencyType fixedCurrencyType;
        CurrencyType floatCurrencyType;
        doBaseRegistration(kernel,
                           model,
                           bump,
                           warning,
                           bumpUsedOut,
                           fixedCurrencyType,
                           floatCurrencyType);

        kernel->registerDCF(coupon_dcf_str, coupon_date_str, basisType);
    }
};

class ExplicitOneLegSwap : public OldSwap, public ConventionDefaultable_I
{
public:
    struct Check : public OldSwap::Check
    {
        Check(const char* msg, int priority) : OldSwap::Check(msg, priority)
        {
            HiddenFloatBasis::addChecks(*this);
            single_leg_check_s.addChecks(*this, conv_spec_m);
        }
    };

    virtual bool validate()
    {
        if(!OldSwap::validate())
            return false;

        single_leg_m.init(getParameters(), single_leg_check_s, NULL);
        HiddenFloatBasis::init(getParameters());
        return true;
    }

    virtual void doRegistration(const NXKernel_I::Ptr& kernel,
                                Model_I::PtrCRef model,
                                const XL::Bump* bump,
                                ApplicationWarning& warning,
                                int& bumpUsedOut)
    {
        doRegistrationOneLeg(kernel,
                             model,
                             bump,
                             warning,
                             bumpUsedOut,
                             single_leg_m.basisType_m);
    }

    virtual bool hasMaturityTenor() const { return false; }

protected:
    ScheduleInfo::Ptr calc_one_leg_schedule(
        const Date& start,
        const Date& end,
        const Calendar_I::Ptr& fixCalendar,
        const Tenor& fixLag,
        bool fudge_first_fixing) const
    {
        ScheduleMaker sched(nominal_cal_m.get(),
                            start,
                            end,
                            single_leg_m.freq_m,
                            single_leg_m.front_stub_m,
                            single_leg_m.long_stub_m,
                            true,
                            true,
                            false,
                            fixCalendar.get(),
                            fixLag,
                            false,
                            single_leg_m.pay_cal_m.get(),
                            single_leg_m.pay_lag_m,
                            single_leg_m.acc_cal_m.get(),
                            false);

        const vector<Date>* baDates = &sched.getBondAccrualDates();
        bool eom = sched.isEOMDated();

        return ScheduleInfo::Ptr(new ScheduleInfo(sched.getDates(),
                                                  single_leg_m.getBasis(baDates, &end, &eom),
                                                  fudge_first_fixing));
    }

    LegCal single_leg_m;
};

class ExplicitTwoLegSwap : public OldSwap, public ConventionDefaultable_I
{
public:
    struct Check : public OldSwap::Check
    {
        FixedLegConvSpec fixed_leg_conv_spec_m;
        FloatLegConvSpec float_leg_conv_spec_m;

        Check(const char* msg, int priority) :
        OldSwap::Check(msg, priority), fixed_leg_conv_spec_m(irswap_s), float_leg_conv_spec_m(irswap_s)
        {
            addOptional(tkFloatSpread, ParseDouble | ParseString);
            FixingInfo::addChecks(*this, conv_spec_m);
            fixed_leg_check_s.addChecks(*this, fixed_leg_conv_spec_m);
            float_leg_check_s.addChecks(*this, float_leg_conv_spec_m);
        }
    };

    virtual bool validate()
    {
        if(!OldSwap::validate())
            return false;

        const ParseResult& result = getParameters();

        hasTwoLegs_m = true;
        fixed_leg_m.init(result, fixed_leg_check_s, NULL);
        float_leg_m.init(result, float_leg_check_s, &fixed_leg_m);

        fixing_info_m.init(result);

        return true;
    }

    virtual void doRegistration(const NXKernel_I::Ptr& kernel,
                                Model_I::PtrCRef model,
                                const Bump* bump,
                                ApplicationWarning& warning,
                                int& bumpUsedOut)
    {
        doRegistrationTwoLegs(kernel, model, bump, warning, bumpUsedOut,
                              fixed_leg_m.basisType_m,
                              float_leg_m.basisType_m,
                              float_leg_m.basisType_m,
                              &float_leg_m.freq_m,
                              float_leg_m.acc_cal_m.get(),
                              &fixing_info_m.fixing_lag_m,
                              fixing_info_m.fixing_cal_m.get());
    }

    virtual Date getInstrumentMaturity(
        const Date& nowDate,
        const Date& fixDate) const
    {
        Date result = OldSwap::getInstrumentMaturity(nowDate, fixDate);
        Date liborDate = fixDate;
        fixing_info_m.fixing_cal_m->addTenor(liborDate, fixing_info_m.fixing_lag_m);
        float_leg_m.acc_cal_m->addTenor(liborDate, float_leg_m.freq_m);
        if (result < liborDate)
            result = liborDate;

        return result;
    }

protected:
    ScheduleInfo::Ptr calc_two_leg_schedule(Date start, Date end) const
    {
        ScheduleMaker fixed_maker(
            nominal_cal_m.get(),
            start, end, fixed_leg_m.freq_m,
            fixed_leg_m.front_stub_m, fixed_leg_m.long_stub_m,
            true, true, false,
            fixed_leg_m.pay_cal_m.get(),
            Tenor("0d"), false,
            fixed_leg_m.pay_cal_m.get(), fixed_leg_m.pay_lag_m,
            fixed_leg_m.acc_cal_m.get(),
            false);

        ScheduleMaker float_maker(
            nominal_cal_m.get(),
            start, end, float_leg_m.freq_m,
            float_leg_m.front_stub_m, float_leg_m.long_stub_m,
            true, true, false,
            fixing_info_m.fixing_cal_m.get(),
            fixing_info_m.fixing_lag_m, false,
            float_leg_m.pay_cal_m.get(), float_leg_m.pay_lag_m,
            float_leg_m.acc_cal_m.get(),
            false);

        const bool useRateDates =
            !float_leg_m.freq_m.isZeroTenor() &&
            float_leg_m.acc_cal_m && fixing_info_m.fixing_cal_m;

        const vector<Date>* fixedBaDates = &fixed_maker.getBondAccrualDates();
        bool fixedEom = fixed_maker.isEOMDated();
        Basis_I::CPtr fixedBasis = fixed_leg_m.getBasis(fixedBaDates, &end, &fixedEom);

        const vector<Date>* floatBaDates = &float_maker.getBondAccrualDates();
        bool floatEom = float_maker.isEOMDated();
        Basis_I::CPtr floatBasis = float_leg_m.getBasis(floatBaDates, &end, &floatEom);

        return ScheduleInfo::Ptr(new ScheduleInfo(fixed_maker.getDates(),
                                                  fixedBasis,
                                                  float_maker.getDates(),
                                                  floatBasis,
                                                  floatBasis,
                                                  useRateDates,
                                                  float_leg_m.acc_cal_m,
                                                  float_leg_m.freq_m,
                                                  fixing_info_m.fixing_cal_m,
                                                  fixing_info_m.fixing_lag_m,
                                                  false));
    }

    LegCal fixed_leg_m;
    LegCal float_leg_m;
    FixingInfo fixing_info_m;
};

class DateOneLegSwap : public ExplicitOneLegSwap
{
public:
    struct Check : public ExplicitOneLegSwap::Check
    {
        typedef DateOneLegSwap ObjectType;
        Check() : ExplicitOneLegSwap::Check("DEPRECATED IR Swap with a single leg from dates", 80)
        {
            DateRange::addChecks(*this);
        }
    };

    virtual bool validate()
    {
        if(!ExplicitOneLegSwap::validate())
            return false;

        date_range_m.init(getParameters());

        schedule_m = calc_one_leg_schedule(date_range_m.start_date_m,
                                                  date_range_m.end_date_m,
                                                  single_leg_m.pay_cal_m,
                                                  Tenor("0D"), false);
        return true;
    }

protected:

    virtual ScheduleInfo::Ptr calc_schedule(
        const Date& now_date,
        bool fudge_first_fixing) const 
    {
        return calc_one_leg_schedule(date_range_m.start_date_m,
            date_range_m.end_date_m,
            single_leg_m.pay_cal_m,
            Tenor("0D"), fudge_first_fixing);
    }

    virtual void view(ApplicationData& out) const {
        viewWithSchedule(out, schedule_m); 
    }

	ScheduleInfo::Ptr schedule_m;

    DateRange date_range_m;
};

class TenorOneLegSwap : public ExplicitOneLegSwap
{
public:
    struct Check : public ExplicitOneLegSwap::Check
    {
        typedef TenorOneLegSwap ObjectType;
        Check() : ExplicitOneLegSwap::Check("DEPRECATED IR Swap with a single leg from tenors", 70)
        {
            TenorRange::addChecks(*this, conv_spec_m);
            FixingInfo::addChecks(*this, conv_spec_m);
        }
    };

    virtual bool validate()
    {
        if(!ExplicitOneLegSwap::validate())
            return false;

        const ParseResult &result = getParameters();

        tenor_range_m.init(result);
        fixing_info_m.init(result);

        if (result.isDefined(tkEventsRefDate)) {
            refDate_m = result.getDate(tkEventsRefDate);
        }

        return true;
    }

    virtual ScheduleInfo::Ptr calc_schedule(const Date& now_date,
                                            bool fudge_first_fixing) const
    {
        Date start, end;
        fixing_info_m.getBoundaries(start,
                                    end,
                                    refDate_m.getOrElse(now_date),
                                    NULL,
                                    tenor_range_m,
                                    nominal_cal_m.get());

        return calc_one_leg_schedule(start, end,
                                     fixing_info_m.fixing_cal_m,
                                     fixing_info_m.fixing_lag_m,
                                     fudge_first_fixing);
    }

    bool hasMaturityTenor() const
    {
        return true;
    }

    Option<Tenor> getMaturityTenor() const
    {
        return Option<Tenor>(tenor_range_m.end_tenor_m);
    }

    virtual Maturity getMaturity() const
    {
        return Maturity(tenor_range_m.end_tenor_m, fixing_info_m.fixing_cal_m);
    }
protected:

    TenorRange tenor_range_m;
    FixingInfo fixing_info_m;

    Option<Date> refDate_m;
};

class DateTwoLegSwap : public ExplicitTwoLegSwap
{
public:
    struct Check : public ExplicitTwoLegSwap::Check
    {
        typedef DateTwoLegSwap ObjectType;
        Check() : ExplicitTwoLegSwap::Check("DEPRECATED IR Swap with a floating leg from dates", 60)
        {
            DateRange::addChecks(*this);
        }
    };

    virtual bool validate()
    {
        if(!ExplicitTwoLegSwap::validate())
            return false;

        date_range_m.init(getParameters());

        schedule_m = calc_two_leg_schedule(date_range_m.start_date_m,
                                                  date_range_m.end_date_m);
        return true;
    }

    virtual bool hasMaturityTenor() const { return false; }

protected:

    virtual ScheduleInfo::Ptr calc_schedule(
        const Date& now_date,
        bool fudge_first_fixing) const 
    {
        return schedule_m;
    }

    virtual void view(ApplicationData& out) const {
        viewWithSchedule(out, schedule_m); 
    }

	/// This is the actual, bona fide schedule that should never be changed once created.
	/// (despite the fact that it is called a cache, which is historical.).
	ScheduleInfo::Ptr schedule_m;

    DateRange date_range_m;
};

class TenorTwoLegSwap : public ExplicitTwoLegSwap
{
public:
    struct Check : public ExplicitTwoLegSwap::Check
    {
        typedef TenorTwoLegSwap ObjectType;
        Check() : ExplicitTwoLegSwap::Check("DEPRECATED IR Swap with a floating leg from tenors", 50)
        {
            TenorRange::addChecks(*this, conv_spec_m);
        }
    };

    virtual bool validate()
    {
        if(!ExplicitTwoLegSwap::validate())
            return false;

        const ParseResult &result = getParameters();
        tenor_range_m.init(result);

        if (result.isDefined(tkEventsRefDate)) {
            refDate_m = result.getDate(tkEventsRefDate);
        }

        return true;
    }

    virtual ScheduleInfo::Ptr calc_schedule(const Date& now_date,
                                            bool fudge_first_fixing) const
    {
        Date start, end;
        fixing_info_m.getBoundaries(start,
                                    end,
                                    refDate_m.getOrElse(now_date),
                                    float_leg_m.acc_cal_m.get(),
                                    tenor_range_m,
                                    nominal_cal_m.get());

        return calc_two_leg_schedule(start, end);
    }

    bool hasMaturityTenor() const
    {
        return true;
    }

    Option<Tenor> getMaturityTenor() const
    {
        return Option<Tenor>(tenor_range_m.end_tenor_m);
    }

    virtual Maturity getMaturity() const
    {
        // Defaulted to empty Maturity
        return Maturity(tenor_range_m.end_tenor_m, fixing_info_m.fixing_cal_m);
    }

protected:

    TenorRange tenor_range_m;

    Option<Date> refDate_m;
};

class ScheduleOneLegSwap : public OldSwap
{
public:
    struct Check : public OldSwap::Check
    {
        typedef ScheduleOneLegSwap ObjectType;
        Check() : OldSwap::Check("DEPRECATED IR Swap with a single leg from an events schedule", 90)
        {
            addRequired(tkObjectEvents, ParseID);
            addRequired(tkCommonBasis, ParseString);
            HiddenFloatBasis::addChecks(*this);
        }
    };

    virtual bool validate()
    {
        if(!OldSwap::validate())
            return false;

        const ParseResult& result = getParameters();
        EventsInterest::Ptr events = result.getPtr(tkObjectEvents);
        HiddenFloatBasis::init(result);
        BasisType basis_type = result.getBasis(tkCommonBasis);

        schedule_m = ScheduleInfo::Ptr(new ScheduleInfo(events->getPeriods(),
                                                               events->getBasis(basis_type),
                                                               false));

        return true;
    }

    virtual void doRegistration(const NXKernel_I::Ptr& kernel,
                                Model_I::PtrCRef model,
                                const Bump* bump,
                                ApplicationWarning& warning,
                                int& bumpUsedOut)
    {
        doRegistrationOneLeg(kernel, model, bump, warning, bumpUsedOut,
                             schedule_m->fixed_m.basis_m->getBasisType());
    }

    virtual bool hasMaturityTenor() const { return false; }

protected:

    virtual ScheduleInfo::Ptr calc_schedule(
        const Date& now_date,
        bool fudge_first_fixing) const 
    {
        return schedule_m;
    }

    virtual void view(ApplicationData& out) const {
        viewWithSchedule(out, schedule_m); 
    }

	ScheduleInfo::Ptr schedule_m;
};

class ScheduleTwoLegSwap : public OldSwap
{
public:
    struct Check : public OldSwap::Check
    {
        typedef ScheduleTwoLegSwap ObjectType;
        Check() : OldSwap::Check("DEPRECATED IR Swap with a floating leg from event schedules", 65)
        {
            addRequired(tkInstrumentFixedEvents, ParseID);
            addRequired(tkFloatEvents, ParseID);
            addRequired(tkCommonBasis, ParseString);
            addOptional(tkFloatBasis, ParseString);
            addOptional(tkFloatSpread, ParseDouble | ParseString);
        }
    };

    virtual bool validate()
    {
        if(!OldSwap::validate())
            return false;

        const ParseResult& result = getParameters();
        EventsInterest::Ptr fixed_events = result.getPtr(tkInstrumentFixedEvents);
        EventsInterest::Ptr float_events = result.getPtr(tkFloatEvents);

        BasisType basis_type = result.getBasis(tkCommonBasis);
        BasisType float_basis_type = result.getBasis(tkFloatBasis, basis_type);
        Basis_I::CPtr floatBasis = float_events->getBasis(float_basis_type);

        // Use empty inputs.  We will not be fixing this.
        const Calendar_I::Ptr null_cal;
        Tenor emptyTenor;
        schedule_m = ScheduleInfo::Ptr(new ScheduleInfo(fixed_events->getPeriods(),
                                                               fixed_events->getBasis(basis_type),
                                                               float_events->getPeriods(),
                                                               floatBasis,
                                                               floatBasis,
                                                               false,
                                                               null_cal,
                                                               emptyTenor,
                                                               null_cal,
                                                               emptyTenor,
                                                               false));

        return true;
    }

    virtual void doRegistration(const NXKernel_I::Ptr& kernel,
                                Model_I::PtrCRef model,
                                const Bump* bump,
                                ApplicationWarning& warning,
                                int& bumpUsedOut)
    {
        doRegistrationTwoLegs(kernel, model, bump, warning, bumpUsedOut,
                              schedule_m->fixed_m.basis_m->getBasisType(),
                              schedule_m->float_m.floatBasis_m->getBasisType(),
                              schedule_m->float_m.floatBasis_m->getBasisType(),
                              NULL, NULL, NULL, NULL);
    }

    virtual bool hasMaturityTenor() const { return false; }

protected:

    virtual ScheduleInfo::Ptr calc_schedule(
        const Date& now_date,
        bool fudge_first_fixing) const 
    {
        return schedule_m;
    }

    virtual void view(ApplicationData& out) const {
        viewWithSchedule(out, schedule_m); 
    }

	ScheduleInfo::Ptr schedule_m;
};

bool init_date_one = registerFactory<DateOneLegSwap::Check>();
bool init_tenor_one = registerFactory<TenorOneLegSwap::Check>();
bool init_date_two = registerFactory<DateTwoLegSwap::Check>();
bool init_tenor_two = registerFactory<TenorTwoLegSwap::Check>();
bool init_schedule_one = registerFactory<ScheduleOneLegSwap::Check>();
bool init_schedule_two = registerFactory<ScheduleTwoLegSwap::Check>();

// ======================================================================
// End of deprecated code
// ======================================================================

bool init_two_legs_single_ccy = registerFactory<NewSwapTwoLegsSingleCcy::Check>();
bool init_two_legs_roll_single_ccy = registerFactory<NewSwapTwoLegsRollTypeSingleCcy::Check>();
bool init_events_single_ccy = registerFactory<NewSwapFromEventsSingleCcy::Check>();
bool init_two_legs_cross_ccy = registerFactory<NewSwapTwoLegsCrossCcy::Check>();
bool init_events_cross_ccy = registerFactory<NewSwapFromEventsCrossCcy::Check>();
bool init_timedep_single_ccy = registerFactory<TimeDependentSwapSingleCcy::Check>();
bool init_timedep_nondeliv_single_ccy = registerFactory<TimeDependentNonDeliverableSwapSingleCcy::Check>();
bool init_timedep_cross_ccy = registerFactory<TimeDependentSwapCrossCcy::Check>();
bool init_timedep_nondeliv_cross_ccy = registerFactory<TimeDependentNonDeliverableSwapCrossCcy::Check>();
}

OBJECT_POINTER_USES(IInstrumentIRSwap, InstrumentBase, "IRSwap");
OBJECT_POINTER_USES(InstrumentIRSwap, IInstrumentIRSwap, "IRSwap");
