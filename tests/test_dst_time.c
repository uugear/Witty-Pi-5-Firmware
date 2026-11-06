#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include "dst_time.h"

#define T(y, m, d, h, n, s) dst_calendar_seconds(y, m, d, h, n, s)

static DstRule europe(void) {
    DstRule r = {0};
    r.offset_minutes = 60;
    r.begin = (DstTimeSpec){.month=3,.week=0,.weekday=0,.hour=2};
    r.end = (DstTimeSpec){.month=10,.week=0,.weekday=0,.hour=3};
    return r;
}

static DstRule us(void) {
    DstRule r = {0};
    r.offset_minutes = 60;
    r.begin = (DstTimeSpec){.month=3,.week=2,.weekday=0,.hour=2};
    r.end = (DstTimeSpec){.month=11,.week=1,.weekday=0,.hour=2};
    return r;
}

static DstRule southern(void) {
    DstRule r = {0};
    r.offset_minutes = 60;
    r.begin = (DstTimeSpec){.month=10,.week=1,.weekday=0,.hour=2};
    r.end = (DstTimeSpec){.month=4,.week=1,.weekday=0,.hour=3};
    return r;
}

int main(void) {
    DstRule eu = europe();
    assert(dst_rule_valid(&eu));
    int64_t at; bool entering;
    assert(dst_transition_seconds(&eu,2027,true,&at));
    assert(at == T(2027,3,28,2,0,0));
    assert(dst_transition_seconds(&eu,2027,false,&at));
    assert(at == T(2027,10,31,2,0,0));
    assert(!dst_is_active_standard(&eu,T(2027,3,28,1,59,59)));
    assert(dst_is_active_standard(&eu,T(2027,3,28,2,0,0)));
    assert(dst_is_active_standard(&eu,T(2027,10,31,1,59,59)));
    assert(!dst_is_active_standard(&eu,T(2027,10,31,2,0,0)));
    assert(dst_standard_to_local(&eu,T(2027,3,28,2,0,0)) == T(2027,3,28,3,0,0));
    assert(dst_standard_to_local(&eu,T(2027,10,31,2,0,0)) == T(2027,10,31,2,0,0));
    assert(dst_local_to_standard(&eu,T(2027,3,28,2,30,0)) == T(2027,3,28,2,30,0));
    assert(dst_standard_to_local(&eu,dst_local_to_standard(&eu,T(2027,3,28,2,30,0))) == T(2027,3,28,3,30,0));
    assert(dst_local_to_standard(&eu,T(2027,3,28,3,0,0)) == T(2027,3,28,2,0,0));
    assert(dst_local_to_standard(&eu,T(2027,10,31,2,30,0)) == T(2027,10,31,1,30,0));
    assert(dst_local_to_standard(&eu,T(2027,10,31,3,30,0)) == T(2027,10,31,3,30,0));
    assert(dst_local_to_standard(&eu,T(2027,10,31,2,30,0)) < T(2027,10,31,2,10,0));
    assert(dst_next_transition(&eu,T(2027,3,28,1,59,57),&at,&entering));
    assert(entering && at == T(2027,3,28,2,0,0));
    assert(dst_next_transition(&eu,T(2027,10,31,1,59,57),&at,&entering));
    assert(!entering && at == T(2027,10,31,2,0,0));
    assert(dst_next_transition(&eu,T(2026,11,15,9,0,0),&at,&entering));
    assert(at == T(2027,3,28,2,0,0));

    int64_t up = dst_resolve_ordered_action(&eu,T(2027,3,28,2,30,0),-1,-1);
    int64_t down = dst_resolve_ordered_action(&eu,T(2027,3,28,3,0,0),
                                              T(2027,3,28,2,30,0),up);
    assert(up == T(2027,3,28,2,30,0));
    assert(down == T(2027,3,28,3,0,0));
    assert(dst_standard_to_local(&eu,down)==T(2027,3,28,4,0,0));
    int64_t next_day = dst_resolve_ordered_action(&eu,T(2027,3,29,2,30,0),
                                                  T(2027,3,28,3,0,0),down);
    assert(next_day==T(2027,3,29,1,30,0));

    DstRule usa = us();
    assert(dst_transition_seconds(&usa,2027,true,&at) && at==T(2027,3,14,2,0,0));
    assert(dst_transition_seconds(&usa,2027,false,&at) && at==T(2027,11,7,1,0,0));

    DstRule south = southern();
    assert(dst_is_active_standard(&south,T(2027,1,1,1,0,0)));
    assert(!dst_is_active_standard(&south,T(2027,7,1,1,0,0)));
    assert(dst_is_active_standard(&south,T(2027,12,1,1,0,0)));
    assert(dst_next_transition(&south,T(2026,12,1,1,0,0),&at,&entering));
    assert(!entering && at == T(2027,4,4,2,0,0));

    DstRule invalid = eu;
    invalid.begin.weekday = 7;
    assert(!dst_rule_valid(&invalid));
    invalid = eu;
    invalid.fixed_date = true;
    invalid.begin.date = 31;
    assert(!dst_rule_valid(&invalid));
    invalid = eu;
    invalid.offset_minutes = 0;
    assert(dst_local_to_standard(&invalid,T(2027,10,31,2,30,0)) == T(2027,10,31,2,30,0));

    puts("PASS: EU/US/southern rules, spring gap, autumn first occurrence, year crossing, validation");
    return 0;
}
