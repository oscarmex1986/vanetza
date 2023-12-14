#include "duty_cycle_permit.hpp"
#include "limeric_budget.hpp"
#include <vanetza/common/runtime.hpp>
#include <chrono>
#include <cmath>
#include <vanetza/dcc/fot.hpp>

// The following variables substitute an interface to the cross-layer DCC 
// at the management entity that keeps track of the time to the next
// transmission. The parts where they act are marked as FoT.
int itsCounter = -1; //Keep track of the node id in the simulation
vanetza::Clock::time_point fotArray[40000]; //Keep track of t_go

namespace vanetza
{
namespace dcc
{

namespace
{
constexpr Clock::duration min_interval = std::chrono::milliseconds(25);
constexpr Clock::duration max_interval = std::chrono::seconds(1);
} // namespace

LimericBudget::LimericBudget(const DutyCyclePermit& dcp, const Runtime& rt) :
    m_duty_cycle_permit(dcp), m_runtime(rt),
    m_interval(min_interval), m_tx_start(Clock::time_point::min()),
    m_tx_on(Clock::duration::zero())
{
    itsCounter++; // FoT
    stationId = itsCounter; // FoT
    update();
}

Clock::duration LimericBudget::delay()
{
    Clock::duration delay = Clock::duration::max();
    if (m_runtime.now() >= m_tx_start + m_interval) {
        delay = Clock::duration::zero();
        fotArray[stationId] = m_runtime.now();
    } else {
        delay = m_tx_start + m_interval - m_runtime.now();
        fotArray[stationId] = m_tx_start + m_interval; // FoT
    }

    

    return delay;
}

Clock::duration LimericBudget::interval()
{
    return m_interval;
}

void LimericBudget::notify(Clock::duration tx_on)
{
    m_tx_start = m_runtime.now();
    m_tx_on = tx_on;

    using std::chrono::duration_cast;
    const auto duty_cycle = m_duty_cycle_permit.permitted_duty_cycle();
    const auto interval = duration_cast<Clock::duration>(tx_on / duty_cycle.value());
    m_interval = clamp_interval(interval);
}

void LimericBudget::update()
{
    using std::chrono::duration_cast;
    using FloatingPointDuration = std::chrono::duration<double, Clock::period>;
    const FloatingPointDuration delay = m_tx_start + m_interval - m_runtime.now();
    const double duty_cycle = m_duty_cycle_permit.permitted_duty_cycle().value();

    if (duty_cycle > 0.0) {
        if (delay.count() > 0.0) {
            // Apply equation B.2 of TS 102 687 v1.2.1 if gate is closed at the moment
            const FloatingPointDuration interval = (m_tx_on / duty_cycle) * (delay / m_interval);
            m_interval = clamp_interval(duration_cast<Clock::duration>(interval) + m_runtime.now() - m_tx_start);
        } else {
            // use equation B.1 otherwise
            const FloatingPointDuration interval = m_tx_on / duty_cycle;
            m_interval = clamp_interval(duration_cast<Clock::duration>(interval));
        }
    } else {
        // bail out with maximum interval if duty cycle is not positive
        m_interval = max_interval;
    }
}

Clock::duration LimericBudget::clamp_interval(Clock::duration interval) const
{
    return std::min(std::max(interval, min_interval), max_interval);
}

} // namespace dcc
} // namespace vanetza
