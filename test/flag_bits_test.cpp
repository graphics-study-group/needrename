#include <cassert>
#include "engine/Core/flagbits.h"

enum class Bits {
    a = 0b0001,
    b = 0b0010,
    c = 0b0100,
    d = 0b1000
};

using Flag = Engine::Flags<Bits>;

int main () {
    Flag flagdefault{};
    Flag flaga{Bits::a}, flagb{Bits::b}, flagc{Bits::c}, flagd{Bits::d};
    Flag flagab{Bits::a, Bits::b};
   
    assert(!flagdefault);
    assert(flagab & flaga);
    assert(flagab & Bits::b);

    auto flg{flagab | flagc};
    assert(flg & Bits::a);
    assert(flg & Bits::b);
    assert(flg & Bits::c);
    assert(!(flg & Bits::d));

    flg = {flagab | Bits::c};
    assert(flg & flaga);
    assert(flg & flagb);
    assert(flg & flagc);
    assert(!(flg & flagd));

    flg = flagab;
    flg &= Bits::a;
    assert(flg == Flag{Bits::a});

    flg = flagab;
    flg &= flaga;
    assert(flg == flaga);

    flg |= Bits::b;
    assert(flg == flagab);
    flg |= flagb;
    assert(flg == flagab);

    flg = ~flg;
    assert(flg & Bits::d);
    assert(!(flg & Bits::a));

    Flag f;
    f.Set(Bits::a);
    f.Set(Bits::b);
    f.Mask(Bits::c);
    assert(!f);

    f.Set(Bits::a);
    assert(f.Test(Bits::a));
    assert(!f.Test(Bits::b));
}
