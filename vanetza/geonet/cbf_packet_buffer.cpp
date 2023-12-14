#include <vanetza/common/runtime.hpp>
#include <vanetza/geonet/address.hpp>
#include <vanetza/geonet/cbf_counter.hpp>
#include <vanetza/geonet/cbf_packet_buffer.hpp>
#include <vanetza/geonet/pdu.hpp>
#include <vanetza/units/time.hpp>
#include <cassert>
#include <iterator>
#include <iostream>
#include <vanetza/dcc/fot.hpp> // OAM intento por traer tgo
#include <vanetza/units/frequency.hpp>
#include <vanetza/units/length.hpp>
#include <vanetza/units/time.hpp>

int cbfNodeCounter = -1;
vanetza::Clock::time_point fotArray[40000];



namespace vanetza
{
namespace geonet
{

CbfPacket::CbfPacket(PendingPacket<GbcPdu>&& packet, const MacAddress& sender) :
    m_packet(std::move(packet)), m_sender(sender)
{
}

CbfPacket::CbfPacket(PendingPacket<GbcPdu, const MacAddress&>&& packet, const MacAddress& sender) :
    m_packet(PendingPacket<GbcPdu>(std::move(packet), cBroadcastMacAddress)), m_sender(sender)
{
}

const MacAddress& CbfPacket::sender() const
{
    return m_sender;
}

const Address& CbfPacket::source() const
{
    return m_packet.pdu().extended().source_position.gn_addr;
}

SequenceNumber CbfPacket::sequence_number() const
{
    return m_packet.pdu().extended().sequence_number;
}

uint8_t CbfPacket::hop_limit() const
{
    return m_packet.pdu().basic().hop_limit;
}

Clock::duration CbfPacket::reduce_lifetime(Clock::duration d)
{
    return m_packet.reduce_lifetime(d);
}

std::size_t CbfPacket::length() const
{
    return m_packet.length();
}


CbfPacketBuffer::CbfPacketBuffer(Runtime& rt, TimerCallback cb, std::unique_ptr<CbfCounter> cnt, std::size_t bytes) :
    m_runtime(rt), m_counter(std::move(cnt)),
    m_capacity_bytes(bytes), m_stored_bytes(0),
    m_timer_callback(cb)
{
    cbfNodeCounter = cbfNodeCounter + 1;
    nodeCounter = cbfNodeCounter;
}

CbfPacketBuffer::~CbfPacketBuffer()
{
    m_runtime.cancel(this);
}

bool CbfPacketBuffer::remove(const Identifier& id)
{
    bool packet_dropped = false;

    auto& id_map = m_timers.right;
    auto found = id_map.find(id);
    if (found != id_map.end()) {
        std::cout << " True: " << ++countRealTrue << "\n";
        auto& packet = found->info;
        m_stored_bytes -= packet->length();
        m_counter->remove(id);
        m_packets.erase(packet);
        remove_timer(m_timers.project_left(found));
        packet_dropped = true;
    }

    assert(m_packets.size() == m_timers.size());
    return packet_dropped;
}



bool CbfPacketBuffer::remove_hopcount_distance(const Identifier& id, int flagRemove, Clock::duration newTimeout, Clock::duration maxTimeout)
{
    bool packet_dropped = false;
    
    
    auto& id_map = m_timers.right;
    auto found = id_map.find(id);
    if (found != id_map.end()) {
        auto& packet = found->info;
        // S-FoT+ Get the hop limit from the current packet
        uint8_t currentHopCount = packet->hop_limit();
        if (flagRemove == 1){
            std::cout << "Node: " << nodeCounter << " True: " << ++countFakeFalse << " Fake True:" << countTrue <<  "\n";
            m_stored_bytes -= packet->length();
            m_counter->remove(id);
            m_packets.erase(packet);
            remove_timer(m_timers.project_left(found));
            packet_dropped = true;
        } else {

            const Timer nowPushBack {m_runtime, newTimeout};
            //S-FoT: Geographically Aware Cancellation
            auto expnow = found->second.expiry;
            auto exppush = nowPushBack.expiry;
            Clock::duration diff;
            if(expnow < exppush){
                diff = exppush - expnow;
            } else {
                diff = expnow - exppush;
            }
            Clock::duration cur_tout = found->second.expiry - found->second.start;
            if(diff < maxTimeout){
                if (found->second.expiry < nowPushBack.expiry){
                    update_fot(id,newTimeout);
                }
            } else {
                if (nowPushBack.expiry < found->second.expiry){
                    update_fot(id,newTimeout);
                }
            }
                
            packet_dropped = true;
        }
        
    }

    assert(m_packets.size() == m_timers.size());
    return packet_dropped;
}

bool CbfPacketBuffer::remove_hopcount_distance_na(const Identifier& id, int flagRemove, Clock::duration newTimeout, Clock::duration maxTimeout)
{
    bool packet_dropped = false;
    

    auto& id_map = m_timers.right;
    auto found = id_map.find(id);
    if (found != id_map.end()) {
        auto& packet = found->info;
        // S-FoT+ Get the hop limit from the current packet
        uint8_t currentHopCount = packet->hop_limit();
        if (flagRemove == 1){
            m_stored_bytes -= packet->length();
            m_counter->remove(id);
            m_packets.erase(packet);
            remove_timer(m_timers.project_left(found));
            packet_dropped = true;
        } else {
            const Timer nowPushBack {m_runtime, newTimeout};
            //S-FoT: Geographically Aware Cancellation
            auto expnow = found->second.expiry;
            auto exppush = nowPushBack.expiry;
            Clock::duration diff;
            if(expnow < exppush){
                diff = exppush - expnow;
            } else {
                diff = expnow - exppush;
            }
            Clock::duration cur_tout = found->second.expiry - found->second.start;
            if(diff < maxTimeout){
                if (found->second.expiry < nowPushBack.expiry){
                    update_fot(id,newTimeout);
                }
            } else {
                if (nowPushBack.expiry < found->second.expiry){
                    update_fot(id,newTimeout);
                }
            }
              
            packet_dropped = true;
        }
        
    }

    assert(m_packets.size() == m_timers.size());
    return packet_dropped;
}

void CbfPacketBuffer::remove_timer(typename timer_bimap::left_map::iterator timer_it)
{
    auto& timer_map = m_timers.left;
    auto successor = timer_map.erase(timer_it);
    if (successor == timer_map.begin() && !timer_map.empty()) {
        // erased timer was scheduled one, reschedule timer trigger
        schedule_timer();
    }
}

void CbfPacketBuffer::add(CbfPacket&& packet, Clock::duration timeout)
{
    
    if(timeout <= Clock::duration::zero()) return;
    m_stored_bytes += packet.length();
    const auto first_timer = m_timers.left.begin();

    // do head drop if necessary
    while (m_stored_bytes > m_capacity_bytes && !m_packets.empty()) {
        m_stored_bytes -= m_packets.front().length();
        const auto id = identifier(m_packets.front());
        m_timers.right.erase(id);
        m_counter->remove(id);
        m_packets.pop_front();
    }

    Timer timer = { m_runtime, timeout };
    const Identifier id = identifier(packet);
    packet.first_rec = m_runtime.now();
    packet.tout = timeout;
    m_packets.emplace_back(std::move(packet));
    using timer_value = timer_bimap::value_type;
    auto insertion = m_timers.insert(timer_value { timer, id, std::prev(m_packets.end()) });
    if (!insertion.second) {
        m_stored_bytes -= m_packets.back().length();
        m_packets.pop_back();
    } else {
        m_counter->add(id);
    }

    // first expirying timer has changed (head drop or added packet)
    if (m_timers.left.begin() != first_timer) {
        schedule_timer();
    }
    assert(m_packets.size() == m_timers.size());
}

void CbfPacketBuffer::add_distance(CbfPacket&& packet, Clock::duration timeout, units::Length distance, const Identifier& cbf_id)
{
    
    

    if(timeout <= Clock::duration::zero()) return;
    m_stored_bytes += packet.length();
    const auto first_timer = m_timers.left.begin();

    // do head drop if necessary
    while (m_stored_bytes > m_capacity_bytes && !m_packets.empty()) {
        m_stored_bytes -= m_packets.front().length();
        const auto id = identifier(m_packets.front());
        m_timers.right.erase(id);
        m_counter->remove(id);
        m_packets.pop_front();
    }

    Timer timer = { m_runtime, timeout };
    const Identifier id = identifier(packet);
    //OAM
    lastPacket = cbf_id;
    lastDistance = distance;
    //OAM
    m_packets.emplace_back(std::move(packet));
    using timer_value = timer_bimap::value_type;
    auto insertion = m_timers.insert(timer_value { timer, id, std::prev(m_packets.end()) });
    if (!insertion.second) {
        m_stored_bytes -= m_packets.back().length();
        m_packets.pop_back();
    } else {
        m_counter->add(id);
    }

    // first expirying timer has changed (head drop or added packet)
    if (m_timers.left.begin() != first_timer) {
        schedule_timer();
    }
    assert(m_packets.size() == m_timers.size());
}

void CbfPacketBuffer::update(const Identifier& id, Clock::duration timeout)
{
    auto& id_map = m_timers.right;
    auto found = id_map.find(id);
    if (found != id_map.end()) {
        const Timer& timer = found->second;
        CbfPacket& cbf_packet = *found->info;
        reduce_lifetime(timer, cbf_packet);
        id_map.replace_data(found, Timer { m_runtime, timeout});
        m_counter->increment(id);
    }
}

void CbfPacketBuffer::update_fot(const Identifier& id, Clock::duration timeout)
{
    auto& id_map = m_timers.right;
    auto found = id_map.find(id);
    if (found != id_map.end()) {
        const Timer& timer = found->second;
        CbfPacket& cbf_packet = *found->info;
        id_map.replace_data(found, Timer { m_runtime, timeout});
        
    }
}

boost::optional<CbfPacket> CbfPacketBuffer::fetch(const Identifier& id)
{
    boost::optional<CbfPacket> packet;

    auto& id_map = m_timers.right;
    auto found = id_map.find(id);
    if (found != id_map.end()) {
        const Timer& timer = found->second;
        CbfPacket& cbf_packet = *found->info;
        bool valid_packet = reduce_lifetime(timer, cbf_packet);
        m_stored_bytes -= cbf_packet.length();
        if (valid_packet) {
            packet.emplace(std::move(cbf_packet));
        }
        m_counter->remove(id);
        m_packets.erase(found->info);
        remove_timer(m_timers.project_left(found));
    }

    return packet;
}

const CbfPacket* CbfPacketBuffer::find(const Identifier& id) const
{
    const auto& id_map = m_timers.right;
    auto found = id_map.find(id);
    return found != id_map.end() ? &(*found->info) : nullptr;
}

std::size_t CbfPacketBuffer::counter(const Identifier& id) const
{
    return m_counter->counter(id);
}

void CbfPacketBuffer::flush()
{
    

        // fetch all expired timers
        const Timer now { m_runtime, std::chrono::seconds(0) };
        // S-FoT+ to decide if we are past t_go
        Clock::duration fot = std::max(fotArray[nodeCounter],m_runtime.now()) - m_runtime.now();
        Clock::duration epsilon = std::chrono::milliseconds(1);
        if(fot > Clock::duration::zero()){
            fot = fot + epsilon;
        }
        
        const Timer nowFot { m_runtime, fot };
        
        auto endFot = m_timers.left.upper_bound(nowFot);
        
        
        for (auto it = m_timers.left.begin(); it != endFot; ++it) {
            const Timer& timer = it->first;
            CbfPacket& packet = *it->info;
            bool valid_packet = reduce_lifetime(timer, packet);
            if (valid_packet) {
                update_fot(it->second,fot);
            } 
        }
        
        auto end = m_timers.left.upper_bound(now); 
        for (auto it = m_timers.left.begin(); it != end;) {
            // reduce LT by queuing time
            const Timer& timer = it->first;
            CbfPacket& packet = *it->info;
            bool valid_packet = reduce_lifetime(timer, packet);
            m_stored_bytes -= packet.length();
            if (valid_packet) {
                m_timer_callback(std::move(packet).packet());
            } 

            m_counter->remove(it->second);
            m_packets.erase(it->info);
            it = m_timers.left.erase(it);
        }
        // schedule timer if not empty
        if (!m_timers.empty()) {
            schedule_timer();
        }
    

}



bool CbfPacketBuffer::reduce_lifetime(const Timer& timer, CbfPacket& packet) const
{
    const auto queuing_time = m_runtime.now() - timer.start;
    return packet.reduce_lifetime(queuing_time) > Clock::duration::zero();
}

bool CbfPacketBuffer::reduce_lifetime_fot(const Timer& timer, CbfPacket& packet) const
{
    const auto queuing_time = m_runtime.now() - timer.start;
        if(timer.expiry < fotArray[nodeCounter]){
            const auto fot = std::max(m_runtime.now(),fotArray[nodeCounter]) - m_runtime.now();
            packet.reduce_lifetime(-fot);
            return true;
        } else {
            return packet.reduce_lifetime(queuing_time) > Clock::duration::zero();
        }

        
    
}

void CbfPacketBuffer::schedule_timer()
{
    assert(!m_timers.empty());
    m_runtime.cancel(this);
    Runtime::Callback cb = [this](Clock::time_point) { flush(); };
    m_runtime.schedule(m_timers.left.begin()->first.expiry, cb, this);
}


CbfPacketBuffer::Timer::Timer(const Runtime& rt, Clock::duration timeout) :
    expiry(rt.now() + timeout), start(rt.now())
{
}

bool CbfPacketBuffer::Timer::operator<(const Timer& other) const
{
    return this->expiry < other.expiry;
}

} // namespace geonet
} // namespace vanetza

