#pragma once

#include <siliconforge/core/Types.hpp>
#include <siliconforge/core/loader/ProgramImage.hpp>

#include <vector>

namespace siliconforge::test {

/// Minimal AVR encoder, enough to write instruction-level tests that read like assembly listings.
/// Relative jumps take word offsets as in the instruction set manual (k, with PC <- PC + k + 1).
class AvrAssembler {
public:
    std::vector<u16> words;

    [[nodiscard]] ProgramImage image() const {
        Segment segment{MemorySpace::Program, 0, {}};
        for (auto word : words) {
            segment.bytes.push_back(static_cast<u8>(word));
            segment.bytes.push_back(static_cast<u8>(word >> 8));
        }
        return ProgramImage{{segment}, {}};
    }

    /// Current position, in words.
    [[nodiscard]] int here() const { return static_cast<int>(words.size()); }

    AvrAssembler& raw(u16 word) { words.push_back(word); return *this; }

    // Register-register: 0000 11rd dddd rrrr style.
    AvrAssembler& rr(u16 base, unsigned d, unsigned r) {
        return raw(static_cast<u16>(base | (r & 0x10) << 5 | (d & 0x1F) << 4 | (r & 0x0F)));
    }
    // Register-immediate on r16..r31.
    AvrAssembler& ri(u16 base, unsigned d, unsigned k) { return raw(static_cast<u16>(base | (k & 0xF0) << 4 | (d - 16) << 4 | (k & 0x0F))); }
    // Single register: 1001 010d dddd xxxx style.
    AvrAssembler& one(u16 base, unsigned d) { return raw(static_cast<u16>(base | (d & 0x1F) << 4)); }

    AvrAssembler& nop() { return raw(0x0000); }
    AvrAssembler& add(unsigned d, unsigned r) { return rr(0x0C00, d, r); }
    AvrAssembler& adc(unsigned d, unsigned r) { return rr(0x1C00, d, r); }
    AvrAssembler& sub(unsigned d, unsigned r) { return rr(0x1800, d, r); }
    AvrAssembler& sbc(unsigned d, unsigned r) { return rr(0x0800, d, r); }
    AvrAssembler& and_(unsigned d, unsigned r) { return rr(0x2000, d, r); }
    AvrAssembler& or_(unsigned d, unsigned r) { return rr(0x2800, d, r); }
    AvrAssembler& eor(unsigned d, unsigned r) { return rr(0x2400, d, r); }
    AvrAssembler& mov(unsigned d, unsigned r) { return rr(0x2C00, d, r); }
    AvrAssembler& cp(unsigned d, unsigned r) { return rr(0x1400, d, r); }
    AvrAssembler& cpc(unsigned d, unsigned r) { return rr(0x0400, d, r); }
    AvrAssembler& cpse(unsigned d, unsigned r) { return rr(0x1000, d, r); }
    AvrAssembler& mul(unsigned d, unsigned r) { return rr(0x9C00, d, r); }
    AvrAssembler& muls(unsigned d, unsigned r) { return raw(static_cast<u16>(0x0200 | (d - 16) << 4 | (r - 16))); }
    AvrAssembler& mulsu(unsigned d, unsigned r) { return raw(static_cast<u16>(0x0300 | (d - 16) << 4 | (r - 16))); }
    AvrAssembler& fmul(unsigned d, unsigned r) { return raw(static_cast<u16>(0x0308 | (d - 16) << 4 | (r - 16))); }
    AvrAssembler& fmuls(unsigned d, unsigned r) { return raw(static_cast<u16>(0x0380 | (d - 16) << 4 | (r - 16))); }
    AvrAssembler& movw(unsigned d, unsigned r) { return raw(static_cast<u16>(0x0100 | (d / 2) << 4 | (r / 2))); }

    AvrAssembler& ldi(unsigned d, unsigned k) { return ri(0xE000, d, k); }
    AvrAssembler& subi(unsigned d, unsigned k) { return ri(0x5000, d, k); }
    AvrAssembler& sbci(unsigned d, unsigned k) { return ri(0x4000, d, k); }
    AvrAssembler& andi(unsigned d, unsigned k) { return ri(0x7000, d, k); }
    AvrAssembler& ori(unsigned d, unsigned k) { return ri(0x6000, d, k); }
    AvrAssembler& cpi(unsigned d, unsigned k) { return ri(0x3000, d, k); }

    AvrAssembler& com(unsigned d) { return one(0x9400, d); }
    AvrAssembler& neg(unsigned d) { return one(0x9401, d); }
    AvrAssembler& swap(unsigned d) { return one(0x9402, d); }
    AvrAssembler& inc(unsigned d) { return one(0x9403, d); }
    AvrAssembler& asr(unsigned d) { return one(0x9405, d); }
    AvrAssembler& lsr(unsigned d) { return one(0x9406, d); }
    AvrAssembler& ror(unsigned d) { return one(0x9407, d); }
    AvrAssembler& dec(unsigned d) { return one(0x940A, d); }
    AvrAssembler& push(unsigned d) { return one(0x920F, d); }
    AvrAssembler& pop(unsigned d) { return one(0x900F, d); }

    AvrAssembler& adiw(unsigned d, unsigned k) { return raw(static_cast<u16>(0x9600 | (k & 0x30) << 2 | ((d - 24) / 2) << 4 | (k & 0x0F))); }
    AvrAssembler& sbiw(unsigned d, unsigned k) { return raw(static_cast<u16>(0x9700 | (k & 0x30) << 2 | ((d - 24) / 2) << 4 | (k & 0x0F))); }

    AvrAssembler& in(unsigned d, unsigned a) { return raw(static_cast<u16>(0xB000 | (a & 0x30) << 5 | d << 4 | (a & 0x0F))); }
    AvrAssembler& out(unsigned a, unsigned r) { return raw(static_cast<u16>(0xB800 | (a & 0x30) << 5 | r << 4 | (a & 0x0F))); }
    AvrAssembler& sbi(unsigned a, unsigned b) { return raw(static_cast<u16>(0x9A00 | a << 3 | b)); }
    AvrAssembler& cbi(unsigned a, unsigned b) { return raw(static_cast<u16>(0x9800 | a << 3 | b)); }
    AvrAssembler& sbic(unsigned a, unsigned b) { return raw(static_cast<u16>(0x9900 | a << 3 | b)); }
    AvrAssembler& sbis(unsigned a, unsigned b) { return raw(static_cast<u16>(0x9B00 | a << 3 | b)); }
    AvrAssembler& sbrc(unsigned r, unsigned b) { return raw(static_cast<u16>(0xFC00 | r << 4 | b)); }
    AvrAssembler& sbrs(unsigned r, unsigned b) { return raw(static_cast<u16>(0xFE00 | r << 4 | b)); }
    AvrAssembler& bst(unsigned d, unsigned b) { return raw(static_cast<u16>(0xFA00 | d << 4 | b)); }
    AvrAssembler& bld(unsigned d, unsigned b) { return raw(static_cast<u16>(0xF800 | d << 4 | b)); }

    AvrAssembler& lds(unsigned d, unsigned address) { return one(0x9000, d).raw(static_cast<u16>(address)); }
    AvrAssembler& sts(unsigned address, unsigned r) { return one(0x9200, r).raw(static_cast<u16>(address)); }
    AvrAssembler& ldX(unsigned d) { return one(0x900C, d); }
    AvrAssembler& ldXInc(unsigned d) { return one(0x900D, d); }
    AvrAssembler& ldXDec(unsigned d) { return one(0x900E, d); }
    AvrAssembler& stX(unsigned r) { return one(0x920C, r); }
    AvrAssembler& stXInc(unsigned r) { return one(0x920D, r); }
    AvrAssembler& stYDec(unsigned r) { return one(0x920A, r); }
    AvrAssembler& lddY(unsigned d, unsigned q) { return raw(static_cast<u16>(0x8008 | (q & 0x20) << 8 | (q & 0x18) << 7 | d << 4 | (q & 0x07))); }
    AvrAssembler& stdZ(unsigned q, unsigned r) { return raw(static_cast<u16>(0x8200 | (q & 0x20) << 8 | (q & 0x18) << 7 | r << 4 | (q & 0x07))); }
    AvrAssembler& lpmZInc(unsigned d) { return one(0x9005, d); }

    AvrAssembler& rjmp(int k) { return raw(static_cast<u16>(0xC000 | (k & 0x0FFF))); }
    AvrAssembler& rcall(int k) { return raw(static_cast<u16>(0xD000 | (k & 0x0FFF))); }
    AvrAssembler& jmp(unsigned wordAddress) { return raw(static_cast<u16>(0x940C | (wordAddress >> 16 & 0x3E) << 3 | (wordAddress >> 16 & 1))).raw(static_cast<u16>(wordAddress)); }
    AvrAssembler& call(unsigned wordAddress) { return raw(static_cast<u16>(0x940E | (wordAddress >> 16 & 0x3E) << 3 | (wordAddress >> 16 & 1))).raw(static_cast<u16>(wordAddress)); }
    AvrAssembler& ijmp() { return raw(0x9409); }
    AvrAssembler& icall() { return raw(0x9509); }
    AvrAssembler& ret() { return raw(0x9508); }
    AvrAssembler& reti() { return raw(0x9518); }
    AvrAssembler& brbs(unsigned s, int k) { return raw(static_cast<u16>(0xF000 | (k & 0x7F) << 3 | s)); }
    AvrAssembler& brbc(unsigned s, int k) { return raw(static_cast<u16>(0xF400 | (k & 0x7F) << 3 | s)); }
    AvrAssembler& breq(int k) { return brbs(1, k); }
    AvrAssembler& brne(int k) { return brbc(1, k); }
    AvrAssembler& sei() { return raw(0x9478); }
    AvrAssembler& cli() { return raw(0x94F8); }
    AvrAssembler& sec() { return raw(0x9408); }
    AvrAssembler& clc() { return raw(0x9488); }
    AvrAssembler& set() { return raw(0x9468); }
    AvrAssembler& sleep() { return raw(0x9588); }
    AvrAssembler& break_() { return raw(0x9598); }

    /// Pads with NOPs up to a word address (to place code at an interrupt vector).
    AvrAssembler& org(int wordAddress) {
        while (here() < wordAddress)
            nop();
        return *this;
    }
};

}  // namespace siliconforge::test
