#pragma once
#include <cstdint>
#include <deque>
#include <algorithm>
#include <cmath>
#include <iterator>

// DXGI feedback is delayed. These are acknowledged presentation IDs, not a
// physical driver queue. No clock, sleep, flush or scheduling calls live here.
class PresentationMeasurement {
    struct Ticket {uint32_t id;int64_t due;bool timed;};
    std::deque<Ticket> tickets;
    uint32_t submitted=0,observed=0,anchorRefresh=0;
    int64_t anchorDue=0;
    double period=0;
    bool anchored=false,haveLatency=false;double baselineLatency=0;
public:
    uint64_t confirmed=0,repeatedRefreshes=0,glitches=0,resets=0,submissions=0;
    bool feedback=false,timing=false,haveCounters=false;
    uint32_t pending=0;
    void Reset(bool counters=true,bool keepLatency=false){tickets.clear();submitted=observed=0;anchored=feedback=timing=false;pending=0;if(!keepLatency||counters)haveLatency=false;if(counters){confirmed=repeatedRefreshes=glitches=resets=submissions=0;haveCounters=false;}}
    void Disjoint(){tickets.clear();anchored=feedback=timing=haveLatency=false;pending=0;observed=0;++resets;}
    void Unavailable(){anchored=feedback=timing=false;}
    void Period(double next){if(!std::isfinite(next)||next<=0){period=0;anchored=timing=false;return;}if(period&&std::abs(next/period-1)>.001)Disjoint();period=next;}
    void Submit(uint32_t id,int64_t due,bool timed){
        if(!id||id==submitted)return;
        if(id<submitted)Disjoint();submitted=id;++submissions;
        tickets.push_back({id,due,timed&&due>0});if(tickets.size()>512){tickets.pop_front();anchored=timing=false;}
    }
    void Observe(uint32_t id,uint32_t refresh,bool valid,bool stableCadence=true,int64_t actualQpc=0){
        if(!valid||!id||!refresh||id>submitted){Unavailable();return;}
        feedback=true;pending=submitted-id;if(id==observed)return;
        if(id<observed){Disjoint();return;}observed=id;
        auto ticket=std::find_if(tickets.begin(),tickets.end(),[&](auto& t){return t.id==id;});
        if(ticket==tickets.end()){anchored=timing=false;return;}
        const auto current=*ticket;tickets.erase(tickets.begin(),std::next(ticket));
        if(!stableCadence){anchored=timing=haveLatency=false;return;}
        if(!current.timed||period<=0){anchored=timing=false;return;}
        ++confirmed;
        int64_t late=0;bool assessed=false;
        if(anchored&&current.due>anchorDue&&refresh>=anchorRefresh){
            const auto planned=static_cast<int64_t>(anchorRefresh)+static_cast<int64_t>(std::llround((current.due-anchorDue)/period));
            late=static_cast<int64_t>(refresh)-planned;assessed=true;
            // One refresh of tolerance for phase quantization/compositor jitter.
            // Count extra refreshes only for >1-vsync lateness; ordinary 3:2,
            // 24->144 Hz holds, fields and Smooth Motion use their actual due times.
        }else if(anchored){anchored=false;timing=false;}
        const bool clocked=actualQpc>0;
        const double latency=static_cast<double>(actualQpc)-current.due;
        // After seek/flush retain the established output latency, not the old
        // media timestamp/refresh anchor. This can assess the first returning
        // frame instead of blindly accepting any restart delay as a new normal.
        if(!anchored&&haveLatency&&clocked){late=static_cast<int64_t>(std::llround((latency-baselineLatency)/period));assessed=true;}
        if(assessed){timing=haveCounters=true;if(late>1){repeatedRefreshes+=static_cast<uint64_t>(late);++glitches;anchored=false;}else if(late< -1)anchored=false;}
        if(clocked&&(!assessed||late<=1)){baselineLatency=haveLatency&&!(assessed&&late< -1)?baselineLatency*.9+latency*.1:latency;haveLatency=true;}
        if(!anchored){anchorDue=current.due;anchorRefresh=refresh;anchored=true;}
    }
};

// DirectShow restarts its raw drop counter at OnStartStreaming. Accumulate
// increments across those source epochs instead of losing the previous total.
class DropMeasurement {
    int previous=0;bool seen=false;
public:
    uint64_t total=0;
    void RestartSource(){seen=false;}
    void Reset(){total=0;}
    void Observe(int value){if(value<0)return;total+=!seen||value<previous?static_cast<uint64_t>(value):static_cast<uint64_t>(value-previous);previous=value;seen=true;}
};

class DurationMeasurement {
    std::deque<std::pair<uint64_t,double>> recent;
public:
    uint64_t count=0;double total=0,last=0;
    void Reset(){recent.clear();count=0;total=last=0;}
    void Add(uint64_t now,double ms){if(!std::isfinite(ms)||ms<0)return;last=ms;total+=ms;++count;recent.emplace_back(now,ms);while(!recent.empty()&&(now-recent.front().first>5000||recent.size()>8192))recent.pop_front();}
    double Average()const{return count?total/count:0;}
    double Maximum(uint64_t now)const{double value=0;for(auto& s:recent)if(now-s.first<=5000)value=std::max(value,s.second);return value;}
};
