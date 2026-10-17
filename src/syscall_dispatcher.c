#include "linux_syscall_table.h"
#include "syscalls.h"
#include "elf_loader.h"
#include "patcher.h"
#include <dlfcn.h>

void *g_alt_stack = NULL;
void *g_alt_stack_top = NULL;
#define ALT_STACK_SIZE (1024 * 1024) /* 1MB dedicated signal stack */

__thread void *t_host_stack_top = NULL;

static void ltm_crash_handler(int sig, siginfo_t *si, void *ctx);
void ltm_sig_handler_impl(int sig, siginfo_t *si, void *ctx);

static void ltm_sig_handler(int sig, siginfo_t *si, void *ctx) {
    (void)sig;
    (void)si;
    void *target_sp = ltm_get_signal_stack();
    if (target_sp) {
        ltm_call_on_stack((void *)ltm_sig_handler_impl, sig, si, ctx, target_sp);
    } else {
        ltm_sig_handler_impl(sig, si, ctx);
    }
}

extern void ltm_resume_context_asm(void *state, void *neon, int use_x18) __attribute__((noreturn));

void ltm_resume_context_ex(void *state, void *neon, int use_x18) __attribute__((noreturn));

void ltm_resume_context_ex(void *state, void *neon, int use_x18) {
    if (state) {
        arm_thread_state64_t *s = (arm_thread_state64_t *)state;
        s->__x[18] = ltm_get_guest_x18();
    }
    ltm_resume_context_asm(state, neon, use_x18);
    __builtin_unreachable();
}

void ltm_resume_context(void *state, void *neon) {
    ltm_resume_context_ex(state, neon, 0);
    __builtin_unreachable();
}

static inline uint64_t get_reg(arm_thread_state64_t *s, int r) {
    if (r == 31) return 0;
    if (r == 18) return ltm_get_guest_x18();
    if (r == 29) return s->__fp;
    if (r == 30) return s->__lr;
    return s->__x[r];
}

static inline void set_reg(arm_thread_state64_t *s, int r, uint64_t val) {
    if (r == 31) return;
    if (r == 18) {
        ltm_set_guest_x18(val);
    } else if (r == 29) {
        s->__fp = val;
    } else if (r == 30) {
        s->__lr = val;
    } else {
        s->__x[r] = val;
    }
}

static inline uint32_t get_wreg(arm_thread_state64_t *s, int r) {
    return (uint32_t)get_reg(s, r);
}

static inline void set_wreg(arm_thread_state64_t *s, int r, uint32_t val) {
    set_reg(s, r, (uint64_t)val);
}

static inline uint64_t eval_add_flags(uint64_t a, uint64_t b, uint32_t *cpsr, bool is_64) {
    uint64_t res;
    uint64_t nzcv;
    if (is_64) {
        __asm__ volatile (
            "adds %0, %2, %3\n\t"
            "mrs %1, nzcv\n\t"
            : "=r"(res), "=r"(nzcv)
            : "r"(a), "r"(b)
            : "cc"
        );
    } else {
        uint32_t r32;
        __asm__ volatile (
            "adds %w0, %w2, %w3\n\t"
            "mrs %1, nzcv\n\t"
            : "=r"(r32), "=r"(nzcv)
            : "r"((uint32_t)a), "r"((uint32_t)b)
            : "cc"
        );
        res = r32;
    }
    *cpsr = (*cpsr & 0x0fffffff) | (uint32_t)(nzcv & 0xf0000000);
    return res;
}

static inline uint64_t eval_sub_flags(uint64_t a, uint64_t b, uint32_t *cpsr, bool is_64) {
    uint64_t res;
    uint64_t nzcv;
    if (is_64) {
        __asm__ volatile (
            "subs %0, %2, %3\n\t"
            "mrs %1, nzcv\n\t"
            : "=r"(res), "=r"(nzcv)
            : "r"(a), "r"(b)
            : "cc"
        );
    } else {
        uint32_t r32;
        __asm__ volatile (
            "subs %w0, %w2, %w3\n\t"
            "mrs %1, nzcv\n\t"
            : "=r"(r32), "=r"(nzcv)
            : "r"((uint32_t)a), "r"((uint32_t)b)
            : "cc"
        );
        res = r32;
    }
    *cpsr = (*cpsr & 0x0fffffff) | (uint32_t)(nzcv & 0xf0000000);
    return res;
}

static inline bool condition_holds(uint32_t cond, uint32_t cpsr) {
    bool n = (cpsr >> 31) & 1;
    bool z = (cpsr >> 30) & 1;
    bool c = (cpsr >> 29) & 1;
    bool v = (cpsr >> 28) & 1;
    switch (cond) {
        case 0: return z;
        case 1: return !z;
        case 2: return c;
        case 3: return !c;
        case 4: return n;
        case 5: return !n;
        case 6: return v;
        case 7: return !v;
        case 8: return c && !z;
        case 9: return !c || z;
        case 10: return n == v;
        case 11: return n != v;
        case 12: return !z && (n == v);
        case 13: return z || (n != v);
        default: return true;
    }
}

static inline uint64_t extend_reg(uint64_t val, uint32_t option, uint32_t shift) {
    uint64_t res = 0;
    switch (option) {
        case 0: res = (uint8_t)val; break;         /* UXTB */
        case 1: res = (uint16_t)val; break;        /* UXTH */
        case 2: res = (uint32_t)val; break;        /* UXTW */
        case 3: res = val; break;                  /* UXTX / LSL */
        case 4: res = (int64_t)(int8_t)val; break; /* SXTB */
        case 5: res = (int64_t)(int16_t)val; break;/* SXTH */
        case 6: res = (int64_t)(int32_t)val; break;/* SXTW */
        case 7: res = val; break;                  /* SXTX */
    }
    return res << shift;
}

static inline uint64_t shift_reg(uint64_t val, uint32_t shift_type, uint32_t amount, bool is_64) {
    if (amount == 0) return val;
    if (is_64) {
        switch (shift_type) {
            case 0: return val << amount;
            case 1: return val >> amount;
            case 2: return (uint64_t)((int64_t)val >> amount);
            default: return val;
        }
    } else {
        uint32_t v32 = (uint32_t)val;
        switch (shift_type) {
            case 0: return (uint32_t)(v32 << amount);
            case 1: return (uint32_t)(v32 >> amount);
            case 2: return (uint32_t)((int32_t)v32 >> amount);
            default: return v32;
        }
    }
}

static inline uint64_t aarch64_decode_bit_masks(uint32_t N, uint32_t imms, uint32_t immr, bool sf) {
    uint32_t val = (N << 6) | ((~imms) & 0x3f);
    int len_bit = -1;
    for (int i = 7; i >= 0; i--) {
        if (val & (1 << i)) {
            len_bit = i;
            break;
        }
    }
    if (len_bit < 1) return 0;
    uint32_t esize = 1U << len_bit;
    uint32_t levels = esize - 1;
    uint32_t s = imms & levels;
    uint32_t r = immr & levels;
    uint64_t pattern = (s == 63) ? ~0ULL : ((1ULL << (s + 1)) - 1);
    r = r % esize;
    uint64_t mask = (pattern >> r) | ((pattern << (esize - r)) & ((esize == 64) ? ~0ULL : ((1ULL << esize) - 1)));
    uint64_t result = 0;
    uint32_t total_bits = sf ? 64 : 32;
    for (uint32_t i = 0; i < total_bits; i += esize) {
        result |= (mask << i);
    }
    return result;
}

void emulate_x18_instruction(void *state_ptr, void *neon_ptr, uint32_t insn) {
    arm_thread_state64_t *state = (arm_thread_state64_t *)state_ptr;
    arm_neon_state64_t *neon = (arm_neon_state64_t *)neon_ptr;
    uint32_t rd  = insn & 0x1f;
    uint32_t rn  = (insn >> 5) & 0x1f;
    uint32_t rt2 = (insn >> 10) & 0x1f;
    uint32_t rm  = (insn >> 16) & 0x1f;
    bool sf = (insn & (1U << 31)) != 0;

    /* Advance PC by 4 by default (branches may override) */
    state->__pc += 4;

    /* 1. Move wide immediate (MOVZ, MOVN, MOVK) */
    if ((insn & 0x1f800000) == 0x12800000 || (insn & 0x1f800000) == 0x52800000 || (insn & 0x1f800000) == 0x72800000) {
        uint32_t opc = (insn >> 29) & 0x3;
        uint32_t hw = (insn >> 21) & 0x3;
        uint64_t imm16 = (insn >> 5) & 0xffff;
        uint32_t shift = hw * 16;
        uint64_t val = 0;
        if (opc == 0) { /* MOVN */
            val = ~(imm16 << shift);
            if (!sf) val = (uint32_t)val;
        } else if (opc == 2) { /* MOVZ */
            val = imm16 << shift;
        } else if (opc == 3) { /* MOVK */
            uint64_t cur = get_reg(state, rd);
            uint64_t mask = ~((uint64_t)0xffff << shift);
            val = (cur & mask) | (imm16 << shift);
        }
        if (sf) set_reg(state, rd, val);
        else set_wreg(state, rd, (uint32_t)val);
        return;
    }

    /* 2. Add / Sub Immediate */
    if ((insn & 0x1f000000) == 0x11000000) {
        uint32_t op = (insn >> 29) & 0x3;
        uint32_t shift_bit = (insn >> 22) & 1;
        uint64_t imm = (insn >> 10) & 0xfff;
        if (shift_bit) imm <<= 12;

        uint64_t a = (rn == 31) ? state->__sp : get_reg(state, rn);
        uint64_t res = 0;

        if (op == 0) { /* ADD */
            res = sf ? (a + imm) : (uint32_t)(a + imm);
            if (rd == 31) state->__sp = res;
            else if (sf) set_reg(state, rd, res);
            else set_wreg(state, rd, (uint32_t)res);
        } else if (op == 1) { /* ADDS / CMN */
            res = eval_add_flags(a, imm, &state->__cpsr, sf);
            if (rd != 31) {
                if (sf) set_reg(state, rd, res);
                else set_wreg(state, rd, (uint32_t)res);
            }
        } else if (op == 2) { /* SUB */
            res = sf ? (a - imm) : (uint32_t)(a - imm);
            if (rd == 31) state->__sp = res;
            else if (sf) set_reg(state, rd, res);
            else set_wreg(state, rd, (uint32_t)res);
        } else if (op == 3) { /* SUBS / CMP */
            res = eval_sub_flags(a, imm, &state->__cpsr, sf);
            if (rd != 31) {
                if (sf) set_reg(state, rd, res);
                else set_wreg(state, rd, (uint32_t)res);
            }
        }
        return;
    }

    /* 3. Add / Sub Register (shifted or extended) */
    if ((insn & 0x1f200000) == 0x0b000000 || (insn & 0x1f200000) == 0x0b200000) {
        uint32_t op = (insn >> 29) & 0x3;
        bool is_extended = (insn & 0x00200000) != 0;
        uint64_t b = get_reg(state, rm);
        if (is_extended) {
            uint32_t opt = (insn >> 13) & 0x7;
            uint32_t sh = (insn >> 10) & 0x7;
            b = extend_reg(b, opt, sh);
        } else {
            uint32_t st = (insn >> 22) & 0x3;
            uint32_t sh = (insn >> 10) & 0x3f;
            b = shift_reg(b, st, sh, sf);
        }
        uint64_t a = (rn == 31 && is_extended) ? state->__sp : get_reg(state, rn);
        uint64_t res = 0;

        if (op == 0) { /* ADD */
            res = sf ? (a + b) : (uint32_t)(a + b);
            if (rd == 31) state->__sp = res;
            else if (sf) set_reg(state, rd, res);
            else set_wreg(state, rd, (uint32_t)res);
        } else if (op == 1) { /* ADDS / CMN */
            res = eval_add_flags(a, b, &state->__cpsr, sf);
            if (rd != 31) {
                if (sf) set_reg(state, rd, res);
                else set_wreg(state, rd, (uint32_t)res);
            }
        } else if (op == 2) { /* SUB */
            res = sf ? (a - b) : (uint32_t)(a - b);
            if (rd == 31) state->__sp = res;
            else if (sf) set_reg(state, rd, res);
            else set_wreg(state, rd, (uint32_t)res);
        } else if (op == 3) { /* SUBS / CMP */
            res = eval_sub_flags(a, b, &state->__cpsr, sf);
            if (rd != 31) {
                if (sf) set_reg(state, rd, res);
                else set_wreg(state, rd, (uint32_t)res);
            }
        }
        return;
    }

    /* 4. Logical Register (shifted register) */
    if ((insn & 0x1f000000) == 0x0a000000) {
        uint32_t opc = (insn >> 29) & 0x3;
        bool N = (insn & (1 << 21)) != 0;
        uint32_t st = (insn >> 22) & 0x3;
        uint32_t sh = (insn >> 10) & 0x3f;
        uint64_t b = shift_reg(get_reg(state, rm), st, sh, sf);
        if (N) b = ~b;
        uint64_t a = get_reg(state, rn);
        uint64_t res = 0;

        if (opc == 0) res = a & b;       /* AND / BIC */
        else if (opc == 1) res = a | b;  /* ORR / ORN (mov) */
        else if (opc == 2) res = a ^ b;  /* EOR / EON */
        else if (opc == 3) {             /* ANDS / BICS (TST) */
            res = a & b;
            state->__cpsr &= 0x0fffffff;
            if (sf) {
                if ((int64_t)res < 0) state->__cpsr |= 0x80000000;
                if (res == 0) state->__cpsr |= 0x40000000;
            } else {
                if ((int32_t)res < 0) state->__cpsr |= 0x80000000;
                if ((uint32_t)res == 0) state->__cpsr |= 0x40000000;
            }
        }

        if (opc != 3 || rd != 31) {
            if (sf) set_reg(state, rd, res);
            else set_wreg(state, rd, (uint32_t)res);
        }
        return;
    }

    /* 4b. Logical (immediate) */
    if ((insn & 0x1f800000) == 0x12000000) {
        uint32_t opc = (insn >> 29) & 0x3;
        uint32_t N = (insn >> 22) & 1;
        uint32_t immr = (insn >> 16) & 0x3f;
        uint32_t imms = (insn >> 10) & 0x3f;
        uint64_t imm = aarch64_decode_bit_masks(N, imms, immr, sf);
        uint64_t a = (rn == 31) ? state->__sp : (sf ? get_reg(state, rn) : get_wreg(state, rn));
        uint64_t res = 0;

        if (opc == 0) res = a & imm;       /* AND */
        else if (opc == 1) res = a | imm;  /* ORR */
        else if (opc == 2) res = a ^ imm;  /* EOR */
        else if (opc == 3) {               /* ANDS (TST) */
            res = a & imm;
            state->__cpsr &= 0x0fffffff;
            if (sf) {
                if ((int64_t)res < 0) state->__cpsr |= 0x80000000;
                if (res == 0) state->__cpsr |= 0x40000000;
            } else {
                if ((int32_t)res < 0) state->__cpsr |= 0x80000000;
                if ((uint32_t)res == 0) state->__cpsr |= 0x40000000;
            }
        }

        if (opc != 3 || rd != 31) {
            if (rd == 31) state->__sp = res;
            else if (sf) set_reg(state, rd, res);
            else set_wreg(state, rd, (uint32_t)res);
        }
        return;
    }

    /* 4c. Extract (EXTR) */
    if ((insn & 0x1f800000) == 0x13800000) {
        uint32_t lsb = (insn >> 10) & 0x3f;
        uint64_t val_n = get_reg(state, rn);
        uint64_t val_m = get_reg(state, rm);
        uint64_t res = 0;
        if (sf) {
            res = (lsb == 0) ? val_n : ((val_m << (64 - lsb)) | (val_n >> lsb));
            set_reg(state, rd, res);
        } else {
            uint32_t vn = (uint32_t)val_n;
            uint32_t vm = (uint32_t)val_m;
            uint32_t r32 = (lsb == 0) ? vn : ((vm << (32 - lsb)) | (vn >> lsb));
            set_wreg(state, rd, r32);
        }
        return;
    }

    /* 4d. Add / Sub with Carry (ADC, ADCS, SBC, SBCS) */
    if ((insn & 0x1fe0fc00) == 0x1a000000) {
        uint32_t op = (insn >> 30) & 1; /* 0 = ADC, 1 = SBC */
        uint32_t S  = (insn >> 29) & 1; /* 1 = set flags */
        bool c_in = (state->__cpsr & 0x20000000) != 0;
        uint64_t a = sf ? get_reg(state, rn) : get_wreg(state, rn);
        uint64_t b = sf ? get_reg(state, rm) : get_wreg(state, rm);
        uint64_t res = 0;
        if (op == 0) { /* ADC */
            uint64_t carry = c_in ? 1 : 0;
            res = a + b + carry;
            if (S) {
                uint32_t nzcv = 0;
                bool cout = sf ? ((res < a) || (carry && res == a)) : ((uint32_t)res < (uint32_t)a || (carry && (uint32_t)res == (uint32_t)a));
                bool vout = sf ? (((~(a ^ b) & (a ^ res)) >> 63) & 1) : (((~(a ^ b) & (a ^ res)) >> 31) & 1);
                if (sf) {
                    if ((int64_t)res < 0) nzcv |= 0x80000000;
                    if (res == 0) nzcv |= 0x40000000;
                } else {
                    if ((int32_t)res < 0) nzcv |= 0x80000000;
                    if ((uint32_t)res == 0) nzcv |= 0x40000000;
                }
                if (cout) nzcv |= 0x20000000;
                if (vout) nzcv |= 0x10000000;
                state->__cpsr = (state->__cpsr & 0x0fffffff) | nzcv;
            }
        } else { /* SBC */
            uint64_t borrow = c_in ? 0 : 1;
            res = a - b - borrow;
            if (S) {
                uint32_t nzcv = 0;
                bool cout = sf ? (a >= b + borrow && (b + borrow >= b)) : ((uint32_t)a >= (uint32_t)b + borrow && ((uint32_t)b + borrow >= (uint32_t)b));
                bool vout = sf ? ((((a ^ b) & (a ^ res)) >> 63) & 1) : ((((a ^ b) & (a ^ res)) >> 31) & 1);
                if (sf) {
                    if ((int64_t)res < 0) nzcv |= 0x80000000;
                    if (res == 0) nzcv |= 0x40000000;
                } else {
                    if ((int32_t)res < 0) nzcv |= 0x80000000;
                    if ((uint32_t)res == 0) nzcv |= 0x40000000;
                }
                if (cout) nzcv |= 0x20000000;
                if (vout) nzcv |= 0x10000000;
                state->__cpsr = (state->__cpsr & 0x0fffffff) | nzcv;
            }
        }
        if (sf) set_reg(state, rd, res);
        else set_wreg(state, rd, (uint32_t)res);
        return;
    }

    /* 5. Conditional Select (CSEL, CSINC, CSINV, CSNEG) */
    if ((insn & 0x1fe00000) == 0x1a800000) {
        uint32_t cond = (insn >> 12) & 0xf;
        uint32_t op = (insn >> 10) & 0x3;
        bool cond_ok = condition_holds(cond, state->__cpsr);
        uint64_t val_n = sf ? get_reg(state, rn) : get_wreg(state, rn);
        uint64_t val_m = sf ? get_reg(state, rm) : get_wreg(state, rm);
        uint64_t res = 0;

        if (cond_ok) {
            res = val_n;
        } else {
            if (op == 0) res = val_m;          /* CSEL */
            else if (op == 1) res = val_m + 1; /* CSINC (CINC, CSET) */
            else if (op == 2) res = ~val_m;    /* CSINV (CINV) */
            else if (op == 3) res = -val_m;    /* CSNEG (CNEG) */
        }
        if (sf) set_reg(state, rd, res);
        else set_wreg(state, rd, (uint32_t)res);
        return;
    }

    /* 6. CCMP / CCMN (Conditional Compare) */
    if ((insn & 0x1fc00000) == 0x1a400000) {
        uint32_t cond = (insn >> 12) & 0xf;
        bool cond_ok = condition_holds(cond, state->__cpsr);
        if (cond_ok) {
            bool is_imm = (insn & (1 << 11)) != 0;
            bool is_ccmn = !(insn & (1 << 30));
            uint64_t a = sf ? get_reg(state, rn) : get_wreg(state, rn);
            uint64_t b = is_imm ? ((insn >> 16) & 0x1f) : (sf ? get_reg(state, rm) : get_wreg(state, rm));
            if (is_ccmn) {
                eval_add_flags(a, b, &state->__cpsr, sf);
            } else {
                eval_sub_flags(a, b, &state->__cpsr, sf);
            }
        } else {
            uint32_t nzcv_imm = insn & 0xf;
            state->__cpsr = (state->__cpsr & 0x0fffffff) | (nzcv_imm << 28);
        }
        return;
    }

    /* 6b. Variable Shifts (LSLV, LSRV, ASRV, RORV) */
    if ((insn & 0x7fe0f000) == 0x1ac02000) {
        uint32_t op = (insn >> 10) & 0x3;
        uint64_t a = sf ? get_reg(state, rn) : get_wreg(state, rn);
        uint64_t b = sf ? get_reg(state, rm) : get_wreg(state, rm);
        uint32_t shift = (uint32_t)(b % (sf ? 64 : 32));
        uint64_t res = 0;
        if (op == 0) { /* LSLV */
            res = sf ? (a << shift) : (uint32_t)((uint32_t)a << shift);
        } else if (op == 1) { /* LSRV */
            res = sf ? (a >> shift) : (uint32_t)((uint32_t)a >> shift);
        } else if (op == 2) { /* ASRV */
            res = sf ? (uint64_t)((int64_t)a >> shift) : (uint32_t)((int32_t)a >> shift);
        } else if (op == 3) { /* RORV */
            if (sf) {
                res = (shift == 0) ? a : ((a >> shift) | (a << (64 - shift)));
            } else {
                uint32_t a32 = (uint32_t)a;
                res = (shift == 0) ? a32 : ((a32 >> shift) | (a32 << (32 - shift)));
            }
        }
        if (sf) set_reg(state, rd, res);
        else set_wreg(state, rd, (uint32_t)res);
        return;
    }

    /* 6c. Data Processing 1-Source (RBIT, CLZ, CLS, REV, REV16, REV32) */
    if ((insn & 0x5fe00000) == 0x5ac00000) {
        uint32_t opcode = (insn >> 10) & 0x3f;
        uint64_t a = sf ? get_reg(state, rn) : get_wreg(state, rn);
        uint64_t res = 0;
        if (opcode == 0) { /* RBIT */
            if (sf) {
                for (int i = 0; i < 64; i++) if (a & (1ULL << i)) res |= (1ULL << (63 - i));
            } else {
                for (int i = 0; i < 32; i++) if (a & (1ULL << i)) res |= (1ULL << (31 - i));
            }
        } else if (opcode == 1) { /* REV16 */
            if (sf) {
                res = (((a & 0xFF00FF00FF00FF00ULL) >> 8) | ((a & 0x00FF00FF00FF00FFULL) << 8));
            } else {
                uint32_t a32 = (uint32_t)a;
                res = (((a32 & 0xFF00FF00U) >> 8) | ((a32 & 0x00FF00FFU) << 8));
            }
        } else if (opcode == 2) { /* REV */
            if (sf) res = __builtin_bswap64(a);
            else res = __builtin_bswap32((uint32_t)a);
        } else if (opcode == 3 && sf) { /* REV32 */
            res = ((uint64_t)__builtin_bswap32((uint32_t)(a >> 32)) << 32) | __builtin_bswap32((uint32_t)a);
        } else if (opcode == 4) { /* CLZ */
            if (sf) res = a ? (uint64_t)__builtin_clzll(a) : 64;
            else res = (uint32_t)a ? (uint32_t)__builtin_clz((uint32_t)a) : 32;
        } else if (opcode == 5) { /* CLS */
            if (sf) res = __builtin_clrsbll((int64_t)a);
            else res = __builtin_clrsb((int32_t)a);
        }
        if (sf) set_reg(state, rd, res);
        else set_wreg(state, rd, (uint32_t)res);
        return;
    }

    /* 7. Multiply / Divide (MADD, MSUB, UMULH, SMULH, UMADDL, UMSUBL, SMADDL, SMSUBL, UDIV, SDIV) */
    if ((insn & 0x1f000000) == 0x1b000000) {
        uint32_t op31 = (insn >> 21) & 0x7;
        uint32_t o0 = (insn >> 15) & 1;
        uint64_t a = get_reg(state, rn);
        uint64_t b = get_reg(state, rm);
        uint64_t c = get_reg(state, rt2);
        uint64_t res = 0;

        if (op31 == 0) {
            if (o0 == 0) { /* MADD */
                res = sf ? (c + a * b) : (uint32_t)(c + (uint32_t)a * (uint32_t)b);
            } else {       /* MSUB */
                res = sf ? (c - a * b) : (uint32_t)(c - (uint32_t)a * (uint32_t)b);
            }
            if (sf) set_reg(state, rd, res);
            else set_wreg(state, rd, (uint32_t)res);
        } else if (op31 == 1) {
            if (o0 == 0) { /* SMADDL */
                res = c + (int64_t)(int32_t)a * (int64_t)(int32_t)b;
            } else {       /* SMSUBL */
                res = c - (int64_t)(int32_t)a * (int64_t)(int32_t)b;
            }
            set_reg(state, rd, res);
        } else if (op31 == 2 && sf) { /* SMULH */
            res = (uint64_t)(((__int128_t)(int64_t)a * (__int128_t)(int64_t)b) >> 64);
            set_reg(state, rd, res);
        } else if (op31 == 5) {
            if (o0 == 0) { /* UMADDL */
                res = c + (uint64_t)(uint32_t)a * (uint64_t)(uint32_t)b;
            } else {       /* UMSUBL */
                res = c - (uint64_t)(uint32_t)a * (uint64_t)(uint32_t)b;
            }
            set_reg(state, rd, res);
        } else if (op31 == 6 && sf) { /* UMULH */
            res = (uint64_t)(((__uint128_t)a * (__uint128_t)b) >> 64);
            set_reg(state, rd, res);
        }
        return;
    }
    if ((insn & 0x1fe0f800) == 0x1ac00800) { /* UDIV / SDIV */
        uint32_t o0 = (insn >> 10) & 1;
        uint64_t b = get_reg(state, rm);
        uint64_t a = get_reg(state, rn);
        uint64_t res = 0;
        if (o0 == 0) { /* UDIV */
            if (sf) res = (b != 0) ? (a / b) : 0;
            else res = ((uint32_t)b != 0) ? ((uint32_t)a / (uint32_t)b) : 0;
        } else {       /* SDIV */
            if (sf) res = (b != 0) ? ((int64_t)a / (int64_t)b) : 0;
            else res = ((int32_t)b != 0) ? ((int32_t)a / (int32_t)b) : 0;
        }
        if (sf) set_reg(state, rd, res);
        else set_wreg(state, rd, (uint32_t)res);
        return;
    }

    /* 8. Branches (CBZ, CBNZ, TBZ, TBNZ, BR, BLR, RET) */
    if ((insn & 0x7e000000) == 0x34000000) { /* CBZ / CBNZ */
        bool is_cbnz = (insn & (1 << 24)) != 0;
        int64_t imm19 = (insn >> 5) & 0x7ffff;
        if (imm19 & 0x40000) imm19 -= 0x80000;
        uint64_t val = sf ? get_reg(state, rd) : get_wreg(state, rd);
        bool take = is_cbnz ? (val != 0) : (val == 0);
        if (take) {
            state->__pc = (state->__pc - 4) + imm19 * 4;
        }
        return;
    }
    if ((insn & 0x7e000000) == 0x36000000) { /* TBZ / TBNZ */
        bool is_tbnz = (insn & (1 << 24)) != 0;
        uint32_t bit = ((insn >> 19) & 0x1f) | ((insn >> 26) & 0x20);
        int64_t imm14 = (insn >> 5) & 0x3fff;
        if (imm14 & 0x2000) imm14 -= 0x4000;
        uint64_t val = get_reg(state, rd);
        bool bit_set = (val & (1ULL << bit)) != 0;
        bool take = is_tbnz ? bit_set : !bit_set;
        if (take) {
            state->__pc = (state->__pc - 4) + imm14 * 4;
        }
        return;
    }
    if ((insn & 0xfe1ffc1f) == 0xd61f0000) { /* BR / BLR / RET */
        uint32_t opc = (insn >> 21) & 0x3;
        uint64_t target = (rn == 18) ? ltm_get_guest_x18() : get_reg(state, rn);
        if (opc == 1) { /* BLR */
            state->__lr = state->__pc;
        }
        state->__pc = target;
        return;
    }

    /* 9. ADR / ADRP */
    if ((insn & 0x1f000000) == 0x10000000) {
        bool is_adrp = (insn & (1U << 31)) != 0;
        uint64_t immlo = (insn >> 29) & 0x3;
        uint64_t immhi = (insn >> 5) & 0x7ffff;
        int64_t imm21 = (immhi << 2) | immlo;
        if (imm21 & 0x100000) imm21 -= 0x200000;

        uint64_t pc = state->__pc - 4;
        uint64_t res = is_adrp ? ((pc & ~0xfffULL) + (imm21 << 12)) : (pc + imm21);
        set_reg(state, rd, res);
        return;
    }

    /* 10. Loads and Stores (LDR, STR, LDP, STP, LDUR, STUR, LDRB, STRB, LDRH, STRH) */
    if ((insn & 0x0a000000) == 0x08000000) {
        uint64_t base = (rn == 31) ? state->__sp : get_reg(state, rn);
        uint64_t ea = base;

        /* Load/Store Pair */
        if ((insn & 0x3a000000) == 0x28000000) {
            bool is_load = (insn & (1 << 22)) != 0;
            int64_t imm7 = (insn >> 15) & 0x7f;
            if (imm7 & 0x40) imm7 -= 0x80;
            uint32_t scale = sf ? 3 : 2;
            int64_t offset = imm7 << scale;
            uint32_t mode = (insn >> 23) & 0x3; /* 1: post-index, 2: signed offset, 3: pre-index */

            if (mode == 1) ea = base;
            else ea = base + offset;

            if (is_load) {
                if (sf) {
                    set_reg(state, rd, *(uint64_t *)ea);
                    set_reg(state, rt2, *(uint64_t *)(ea + 8));
                } else {
                    set_wreg(state, rd, *(uint32_t *)ea);
                    set_wreg(state, rt2, *(uint32_t *)(ea + 4));
                }
            } else {
                if (sf) {
                    *(uint64_t *)ea = get_reg(state, rd);
                    *(uint64_t *)(ea + 8) = get_reg(state, rt2);
                } else {
                    *(uint32_t *)ea = get_wreg(state, rd);
                    *(uint32_t *)(ea + 4) = get_wreg(state, rt2);
                }
            }

            if (mode == 1) base += offset;
            else if (mode == 3) base += offset;
            if (mode == 1 || mode == 3) {
                if (rn == 31) state->__sp = base;
                else set_reg(state, rn, base);
            }
            return;
        }

        /* Register offset */
        if ((insn & 0x3b200c00) == 0x38200800) {
            uint32_t opt = (insn >> 13) & 0x7;
            bool S = (insn & (1 << 12)) != 0;
            uint32_t size = (insn >> 30) & 0x3;
            uint32_t shift = S ? size : 0;
            uint64_t offset = extend_reg(get_reg(state, rm), opt, shift);
            ea = base + offset;
        } else if ((insn & 0x3b200000) == 0x38000000) {
            /* Unscaled immediate (LDUR / STUR) */
            int64_t imm9 = (insn >> 12) & 0x1ff;
            if (imm9 & 0x100) imm9 -= 0x200;
            ea = base + imm9;
        } else {
            /* Scaled unsigned immediate offset */
            uint32_t size = (insn >> 30) & 0x3;
            uint64_t imm12 = (insn >> 10) & 0xfff;
            ea = base + (imm12 << size);
        }

        uint32_t size = (insn >> 30) & 0x3;
        uint32_t opc = (insn >> 22) & 0x3;
        bool is_load = (opc != 0);

        if (size == 0) { /* 8-bit */
            if (is_load) {
                uint8_t byte = *(uint8_t *)ea;
                if (opc == 2) set_reg(state, rd, (int64_t)(int8_t)byte); /* LDRSB */
                else set_wreg(state, rd, byte); /* LDRB */
            } else {
                *(uint8_t *)ea = (uint8_t)get_wreg(state, rd); /* STRB */
            }
        } else if (size == 1) { /* 16-bit */
            if (is_load) {
                uint16_t half = *(uint16_t *)ea;
                if (opc == 2) set_reg(state, rd, (int64_t)(int16_t)half); /* LDRSH */
                else set_wreg(state, rd, half); /* LDRH */
            } else {
                *(uint16_t *)ea = (uint16_t)get_wreg(state, rd); /* STRH */
            }
        } else if (size == 2) { /* 32-bit */
            if (is_load) {
                uint32_t word = *(uint32_t *)ea;
                if (opc == 2) set_reg(state, rd, (int64_t)(int32_t)word); /* LDRSW */
                else set_wreg(state, rd, word); /* LDR Wd */
            } else {
                *(uint32_t *)ea = get_wreg(state, rd); /* STR Wd */
            }
        } else if (size == 3) { /* 64-bit */
            if (is_load) {
                set_reg(state, rd, *(uint64_t *)ea); /* LDR Xd */
            } else {
                *(uint64_t *)ea = get_reg(state, rd); /* STR Xd */
            }
        }
        return;
    }

    /* 11. Bitfield (UBFM, SBFM, BFM) */
    if ((insn & 0x1f800000) == 0x53000000 || (insn & 0x1f800000) == 0x13000000) {
        bool is_sbfm = (insn & 0x1f800000) == 0x13000000;
        uint32_t immr = (insn >> 16) & (sf ? 0x3f : 0x1f);
        uint32_t imms = (insn >> 10) & (sf ? 0x3f : 0x1f);
        uint64_t src = sf ? get_reg(state, rn) : get_wreg(state, rn);
        uint64_t res = 0;

        if (imms >= immr) {
            /* Bitfield extract: ubfx / sbfx / lsr */
            uint32_t width = imms - immr + 1;
            uint64_t mask = (width == 64) ? ~0ULL : ((1ULL << width) - 1);
            res = (src >> immr) & mask;
            if (is_sbfm && (res & (1ULL << (width - 1)))) {
                /* Sign-extend */
                res |= ~mask;
            }
        } else {
            /* Bitfield insert / LSL / ubfiz */
            uint32_t width = imms + 1;
            uint32_t lshift = (sf ? 64 : 32) - immr;
            uint64_t mask = ((1ULL << width) - 1);
            res = (src & mask) << lshift;
        }

        if (sf) set_reg(state, rd, res);
        else set_wreg(state, rd, (uint32_t)res);
        return;
    }

    /* 12. FP / SIMD to GPR (FMOV) */
    if ((insn & 0x7f200000) == 0x1e200000) {
        uint32_t opcode = (insn >> 16) & 0x7;
        if (opcode == 0) { /* FMOV Wd, Sn */
            uint32_t val = neon ? (uint32_t)neon->__v[rn] : 0;
            set_wreg(state, rd, val);
        } else if (opcode == 1) { /* FMOV Xd, Dn */
            uint64_t val = neon ? (uint64_t)neon->__v[rn] : 0;
            set_reg(state, rd, val);
        } else if (opcode == 4) { /* FMOV Sn, Wn */
            uint32_t val = (uint32_t)get_reg(state, rn);
            if (neon) neon->__v[rd] = val;
        } else if (opcode == 5) { /* FMOV Dn, Xn */
            uint64_t val = get_reg(state, rn);
            if (neon) neon->__v[rd] = val;
        }
        return;
    }

    ltm_log_error("Unhandled x18 emulation instruction 0x%08x at PC=%p", insn, (void *)(state->__pc - 4));
}

void ltm_sig_handler_impl(int sig, siginfo_t *si, void *ctx) {
    ucontext_t *uc = (ucontext_t *)ctx;
    arm_thread_state64_t *state = &uc->uc_mcontext->__ss;
    arm_neon_state64_t *neon = &uc->uc_mcontext->__ns;

    state->__x[18] = ltm_get_guest_x18();

    uint32_t insn = *(uint32_t *)state->__pc;

    /* Handle unpatched svc #0 (0xd4000001) if delivered via SIGILL or SIGSYS */
    if (insn == 0xd4000001U) {
        uint64_t num = state->__x[8];
        uint64_t a0 = state->__x[0];
        uint64_t a1 = state->__x[1];
        uint64_t a2 = state->__x[2];
        uint64_t a3 = state->__x[3];
        uint64_t a4 = state->__x[4];
        uint64_t a5 = state->__x[5];

        int64_t ret = syscall_dispatch(num, a0, a1, a2, a3, a4, a5, state, neon);
        state->__x[0] = (uint64_t)ret;
        state->__pc += 4;
        ltm_resume_context(state, neon);
    }

    if (sig == SIGILL) {
        uint32_t op = insn & ~0x1fU;
        uint32_t reg = insn & 0x1fU;

        if (op == 0xd5380000U) {
            /* mrs Xt, MIDR_EL1 */
            if (reg < 31) set_reg(state, reg, 0x610f0300ULL); /* Apple Silicon M-series */
            state->__pc += 4;
            ltm_resume_context_ex(state, neon, 1);
        } else if (op == 0xd53b0020U) {
            /* mrs Xt, CTR_EL0 */
            if (reg < 31) set_reg(state, reg, 0x8444c004ULL); /* 64-byte I/D cache lines */
            state->__pc += 4;
            ltm_resume_context_ex(state, neon, 1);
        } else if (op == 0xd5380600U) {
            /* mrs Xt, ID_AA64ISAR0_EL1 */
            if (reg < 31) set_reg(state, reg, 0x00011120ULL); /* AES, SHA1, SHA2, CRC32, ATOMICS */
            state->__pc += 4;
            ltm_resume_context_ex(state, neon, 1);
        } else if (op == 0xd5380620U) {
            /* mrs Xt, ID_AA64ISAR1_EL1 */
            if (reg < 31) set_reg(state, reg, 0x00000022ULL); /* DP, FCMA, JSCVT */
            state->__pc += 4;
            ltm_resume_context_ex(state, neon, 1);
        } else if (op == 0xd5380400U) {
            /* mrs Xt, ID_AA64PFR0_EL1 */
            if (reg < 31) set_reg(state, reg, 0x00000011ULL); /* FP, AdvSIMD, AArch64 */
            state->__pc += 4;
            ltm_resume_context_ex(state, neon, 1);
        } else if (op == 0xd5380420U) {
            /* mrs Xt, ID_AA64PFR1_EL1 */
            if (reg < 31) set_reg(state, reg, 0x0ULL);
            state->__pc += 4;
            ltm_resume_context_ex(state, neon, 1);
        } else if (op == 0xd5380700U) {
            /* mrs Xt, ID_AA64MMFR0_EL1 */
            if (reg < 31) set_reg(state, reg, 0x00000005ULL); /* 48-bit VA, 16KB/4KB granule */
            state->__pc += 4;
            ltm_resume_context_ex(state, neon, 1);
        } else if (op == 0xd5380720U) {
            /* mrs Xt, ID_AA64MMFR1_EL1 */
            if (reg < 31) set_reg(state, reg, 0x0ULL);
            state->__pc += 4;
            ltm_resume_context_ex(state, neon, 1);
        } else if (op == 0xd5380500U) {
            /* mrs Xt, ID_AA64DFR0_EL1 */
            if (reg < 31) set_reg(state, reg, 0x00000006ULL);
            state->__pc += 4;
            ltm_resume_context_ex(state, neon, 1);
        }
        ltm_crash_handler(sig, si, ctx);
        return;
    }

    /* Check if it's our virtualized TLS trap */
    if ((insn & 0xffe0001fU) == 0xd4200000U) {
        uint32_t imm = (insn >> 5) & 0xffffU;
        uint32_t type = imm >> 8;
        uint32_t reg = imm & 0x1fU;

        if (type == 1) {
            /* Virtualized TLS read: mrs Xt, TPIDR_EL0 */
            if (reg < 31) {
                uint64_t tls = ltm_get_guest_tls();
                if (reg == 18) {
                    ltm_set_guest_x18(tls);
                } else {
                    state->__x[reg] = tls;
                }
            }
            state->__pc += 4;
            ltm_resume_context_ex(state, neon, 1);
        } else if (type == 2) {
            /* Virtualized TLS write: msr TPIDR_EL0, Xt */
            if (reg < 31) {
                uint64_t v = (reg == 18) ? ltm_get_guest_x18() : state->__x[reg];
                ltm_set_guest_tls(v);
            } else {
                ltm_set_guest_tls(0); /* xzr */
            }
            state->__pc += 4;
            ltm_resume_context_ex(state, neon, 1);
        } else if (imm == 0x1800U) {
            /* Universal Real-Time x18 Virtualization Engine */
            uint32_t orig_insn = x18_lookup_patch(state->__pc);
            if (!orig_insn) {
                ltm_log_error("x18 patch lookup failed for PC=%p", (void *)state->__pc);
                handle_kill(getpid(), SIGTRAP);
                return;
            }
            emulate_x18_instruction(state, neon, orig_insn);
            if (ltm_check_and_deliver_pending_signals(state, neon)) {
                ltm_resume_context(state, neon);
            } else {
                ltm_resume_context_ex(state, neon, 1);
            }
        } else if (imm != 0) {
            ltm_log_error("Unhandled brk #0x%x at PC=%p", imm, (void *)state->__pc);
            handle_kill(getpid(), SIGTRAP);
            return;
        }
    }

    uint64_t num = state->__x[8];
    uint64_t a0 = state->__x[0];
    uint64_t a1 = state->__x[1];
    uint64_t a2 = state->__x[2];
    uint64_t a3 = state->__x[3];
    uint64_t a4 = state->__x[4];
    uint64_t a5 = state->__x[5];

    ltm_set_in_host_syscall(1);
    int64_t ret = syscall_dispatch(num, a0, a1, a2, a3, a4, a5, state, neon);
    ltm_set_in_host_syscall(0);

    if (num == 139 /* rt_sigreturn */ && ret == 0) {
        /* Guest context has been fully restored from sigframe into state & neon (Rule 5) */
        ltm_resume_context(state, neon);
    }

    /* Set guest return value in x0 */
    state->__x[0] = (uint64_t)ret;

    /* Advance PC past brk #0 (4 bytes) */
    state->__pc += 4;

    /* Check for signals that arrived while executing host syscall */
    ltm_check_and_deliver_pending_signals(state, neon);
    ltm_resume_context(state, neon);
}

static void ltm_crash_handler_impl(int sig, siginfo_t *si, void *ctx);

static void ltm_crash_handler(int sig, siginfo_t *si, void *ctx) {
    void *target_sp = ltm_get_signal_stack();
    if (target_sp) {
        ltm_call_on_stack((void *)ltm_crash_handler_impl, sig, si, ctx, target_sp);
    } else {
        ltm_crash_handler_impl(sig, si, ctx);
    }
}

static void ltm_crash_handler_impl(int sig, siginfo_t *si, void *ctx) {
    ucontext_t *uc = (ucontext_t *)ctx;
    arm_thread_state64_t *state = &uc->uc_mcontext->__ss;

    uint64_t cur_tpidr = 0;
    __asm__ volatile("mrs %0, tpidr_el0" : "=r"(cur_tpidr));

    Dl_info dli_pc = {0}, dli_lr = {0}, dli_x16 = {0};
    dladdr((void *)state->__pc, &dli_pc);
    dladdr((void *)state->__lr, &dli_lr);
    dladdr((void *)state->__x[16], &dli_x16);

    fprintf(stderr, "\n[LTM:CRASH] [pid=%d] Fatal signal %d (%s) at address %p (PC=0x%llx [%s], lr=0x%llx [%s], far=0x%llx, esr=0x%x, tpidr_el0=0x%llx)\n",
            getpid(), sig, sig == SIGSEGV ? "SIGSEGV" : (sig == SIGBUS ? "SIGBUS" : (sig == SIGILL ? "SIGILL" : "UNKNOWN")),
            si->si_addr, (unsigned long long)state->__pc, dli_pc.dli_sname ? dli_pc.dli_sname : "?",
            (unsigned long long)state->__lr, dli_lr.dli_sname ? dli_lr.dli_sname : "?",
            (unsigned long long)uc->uc_mcontext->__es.__far, uc->uc_mcontext->__es.__esr,
            (unsigned long long)cur_tpidr);
    fprintf(stderr, "  x16=[%s (%s)] x17=0x%llx\n",
            dli_x16.dli_sname ? dli_x16.dli_sname : "?", dli_x16.dli_fname ? dli_x16.dli_fname : "?",
            (unsigned long long)state->__x[17]);
    for (int i = 0; i < 31; i += 2) {
        fprintf(stderr, "  x%-2d = 0x%016llx  x%-2d = 0x%016llx\n",
                i, (unsigned long long)state->__x[i],
                i+1, (i+1 < 31) ? (unsigned long long)state->__x[i+1] : 0ULL);
    }
    fprintf(stderr, "  sp  = 0x%016llx\n", (unsigned long long)state->__sp);
    fprintf(stderr, "  lr  = 0x%016llx\n", (unsigned long long)state->__lr);

    if (state->__pc >= 0x1000) {
        uint32_t *code = (uint32_t *)(state->__pc - 16);
        fprintf(stderr, "Code around PC:\n");
        for (int i = -4; i <= 4; i++) {
            fprintf(stderr, "  %s 0x%llx: 0x%08x\n", (i == 0 ? "=>" : "  "),
                    (unsigned long long)(state->__pc + i * 4), code[i + 4]);
        }
    }
    if (state->__sp >= 0x100000000ULL) {
        uint64_t *stk = (uint64_t *)state->__sp;
        fprintf(stderr, "Stack dump:\n");
        for (int i = 0; i < 16; i += 2) {
            fprintf(stderr, "  sp+%02x: 0x%016llx 0x%016llx\n",
                    i * 8, (unsigned long long)stk[i], (unsigned long long)stk[i+1]);
        }
    }
    exit(128 + sig);
}

int syscall_dispatcher_init(void) {
    /* Unblock all signals so inherited masks across execve cannot block SIGTRAP */
    sigset_t empty_mask;
    sigemptyset(&empty_mask);
    sigprocmask(SIG_SETMASK, &empty_mask, NULL);

    /* Ignore background terminal signals to prevent background job control suspension */
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    /* Reset any inherited ignored signals across execve (e.g. from interactive bash) to SIG_DFL */
    for (int s = 1; s < NSIG; s++) {
        if (s == SIGKILL || s == SIGSTOP || s == SIGTRAP) continue;
        struct sigaction cur_sa;
        if (sigaction(s, NULL, &cur_sa) == 0) {
            if (cur_sa.sa_handler == SIG_IGN && s != SIGTTOU && s != SIGTTIN) {
                struct sigaction dfl_sa;
                memset(&dfl_sa, 0, sizeof(dfl_sa));
                dfl_sa.sa_handler = SIG_DFL;
                sigaction(s, &dfl_sa, NULL);
            }
        }
    }

    /* Allocate alternate stack for safe signal handling */
    g_alt_stack = mmap(NULL, ALT_STACK_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (g_alt_stack == MAP_FAILED) {
        ltm_log_error("Failed to allocate signal alternate stack: %s", strerror(errno));
        return -1;
    }
    g_alt_stack_top = (char *)g_alt_stack + ALT_STACK_SIZE;
    t_host_stack_top = g_alt_stack_top;

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = ltm_sig_handler;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER; /* NO SA_ONSTACK! Stack switch handled natively via _ltm_call_on_stack */
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGTRAP, &sa, NULL) != 0) {
        ltm_log_error("Failed to register SIGTRAP handler: %s", strerror(errno));
        return -1;
    }

    /* Also register SIGSYS and SIGILL in case of unpatched svc */
    sigaction(SIGSYS, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);

    /* Register crash handlers for diagnostics */
    struct sigaction sa_crash;
    memset(&sa_crash, 0, sizeof(sa_crash));
    sa_crash.sa_sigaction = ltm_crash_handler;
    sa_crash.sa_flags = SA_SIGINFO;
    sigemptyset(&sa_crash.sa_mask);
    sigaction(SIGSEGV, &sa_crash, NULL);
    sigaction(SIGBUS, &sa_crash, NULL);

    ltm_log_debug("Syscall dispatcher initialized successfully (SIGTRAP hooked)");
    return 0;
}

void ltm_dispatcher_reinit_child(void) {
    t_host_stack_top = g_alt_stack_top;
}


/* Syscall Name Lookup Table */
static const char * const g_linux_syscall_names[LINUX_MAX_SYSCALL_NR + 1] = {
    [0] = "io_setup",
    [1] = "io_destroy",
    [2] = "io_submit",
    [3] = "io_cancel",
    [4] = "io_getevents",
    [5] = "setxattr",
    [6] = "lsetxattr",
    [7] = "fsetxattr",
    [8] = "getxattr",
    [9] = "lgetxattr",
    [10] = "fgetxattr",
    [11] = "listxattr",
    [12] = "llistxattr",
    [13] = "flistxattr",
    [14] = "removexattr",
    [15] = "lremovexattr",
    [16] = "fremovexattr",
    [17] = "getcwd",
    [18] = "lookup_dcookie",
    [19] = "eventfd2",
    [20] = "epoll_create1",
    [21] = "epoll_ctl",
    [22] = "epoll_pwait",
    [23] = "dup",
    [24] = "dup3",
    [25] = "fcntl",
    [26] = "inotify_init1",
    [27] = "inotify_add_watch",
    [28] = "inotify_rm_watch",
    [29] = "ioctl",
    [30] = "ioprio_set",
    [31] = "ioprio_get",
    [32] = "flock",
    [33] = "mknodat",
    [34] = "mkdirat",
    [35] = "unlinkat",
    [36] = "symlinkat",
    [37] = "linkat",
    [38] = "renameat",
    [39] = "umount2",
    [40] = "mount",
    [41] = "pivot_root",
    [42] = "nfsservctl",
    [43] = "statfs",
    [44] = "fstatfs",
    [45] = "truncate",
    [46] = "ftruncate",
    [47] = "fallocate",
    [48] = "faccessat",
    [49] = "chdir",
    [50] = "fchdir",
    [51] = "chroot",
    [52] = "fchmod",
    [53] = "fchmodat",
    [54] = "fchownat",
    [55] = "fchown",
    [56] = "openat",
    [57] = "close",
    [58] = "vhangup",
    [59] = "pipe2",
    [60] = "quotactl",
    [61] = "getdents64",
    [62] = "lseek",
    [63] = "read",
    [64] = "write",
    [65] = "readv",
    [66] = "writev",
    [67] = "pread64",
    [68] = "pwrite64",
    [69] = "preadv",
    [70] = "pwritev",
    [71] = "sendfile",
    [72] = "pselect6",
    [73] = "ppoll",
    [74] = "signalfd4",
    [75] = "vmsplice",
    [76] = "splice",
    [77] = "tee",
    [78] = "readlinkat",
    [79] = "newfstatat",
    [80] = "fstat",
    [81] = "sync",
    [82] = "fsync",
    [83] = "fdatasync",
    [84] = "sync_file_range",
    [85] = "timerfd_create",
    [86] = "timerfd_settime",
    [87] = "timerfd_gettime",
    [88] = "utimensat",
    [89] = "acct",
    [90] = "capget",
    [91] = "capset",
    [92] = "personality",
    [93] = "exit",
    [94] = "exit_group",
    [95] = "waitid",
    [96] = "set_tid_address",
    [97] = "unshare",
    [98] = "futex",
    [99] = "set_robust_list",
    [100] = "get_robust_list",
    [101] = "nanosleep",
    [102] = "getitimer",
    [103] = "setitimer",
    [104] = "kexec_load",
    [105] = "init_module",
    [106] = "delete_module",
    [107] = "timer_create",
    [108] = "timer_gettime",
    [109] = "timer_getoverrun",
    [110] = "timer_settime",
    [111] = "timer_delete",
    [112] = "clock_settime",
    [113] = "clock_gettime",
    [114] = "clock_getres",
    [115] = "clock_nanosleep",
    [116] = "syslog",
    [117] = "ptrace",
    [118] = "sched_setparam",
    [119] = "sched_setscheduler",
    [120] = "sched_getscheduler",
    [121] = "sched_getparam",
    [122] = "sched_setaffinity",
    [123] = "sched_getaffinity",
    [124] = "sched_yield",
    [125] = "sched_get_priority_max",
    [126] = "sched_get_priority_min",
    [127] = "sched_rr_get_interval",
    [128] = "restart_syscall",
    [129] = "kill",
    [130] = "tkill",
    [131] = "tgkill",
    [132] = "sigaltstack",
    [133] = "rt_sigsuspend",
    [134] = "rt_sigaction",
    [135] = "rt_sigprocmask",
    [136] = "rt_sigpending",
    [137] = "rt_sigtimedwait",
    [138] = "rt_sigqueueinfo",
    [139] = "rt_sigreturn",
    [140] = "setpriority",
    [141] = "getpriority",
    [142] = "reboot",
    [143] = "setregid",
    [144] = "setgid",
    [145] = "setreuid",
    [146] = "setuid",
    [147] = "setresuid",
    [148] = "getresuid",
    [149] = "setresgid",
    [150] = "getresgid",
    [151] = "setfsuid",
    [152] = "setfsgid",
    [153] = "times",
    [154] = "setpgid",
    [155] = "getpgid",
    [156] = "getsid",
    [157] = "setsid",
    [158] = "getgroups",
    [159] = "setgroups",
    [160] = "uname",
    [161] = "sethostname",
    [162] = "setdomainname",
    [163] = "getrlimit",
    [164] = "setrlimit",
    [165] = "getrusage",
    [166] = "umask",
    [167] = "prctl",
    [168] = "getcpu",
    [169] = "gettimeofday",
    [170] = "settimeofday",
    [171] = "adjtimex",
    [172] = "getpid",
    [173] = "getppid",
    [174] = "getuid",
    [175] = "geteuid",
    [176] = "getgid",
    [177] = "getegid",
    [178] = "gettid",
    [179] = "sysinfo",
    [180] = "mq_open",
    [181] = "mq_unlink",
    [182] = "mq_timedsend",
    [183] = "mq_timedreceive",
    [184] = "mq_notify",
    [185] = "mq_getsetattr",
    [186] = "msgget",
    [187] = "msgctl",
    [188] = "msgrcv",
    [189] = "msgsnd",
    [190] = "semget",
    [191] = "semctl",
    [192] = "semtimedop",
    [193] = "semop",
    [194] = "shmget",
    [195] = "shmctl",
    [196] = "shmat",
    [197] = "shmdt",
    [198] = "socket",
    [199] = "socketpair",
    [200] = "bind",
    [201] = "listen",
    [202] = "accept",
    [203] = "connect",
    [204] = "getsockname",
    [205] = "getpeername",
    [206] = "sendto",
    [207] = "recvfrom",
    [208] = "setsockopt",
    [209] = "getsockopt",
    [210] = "shutdown",
    [211] = "sendmsg",
    [212] = "recvmsg",
    [213] = "readahead",
    [214] = "brk",
    [215] = "munmap",
    [216] = "mremap",
    [217] = "add_key",
    [218] = "request_key",
    [219] = "keyctl",
    [220] = "clone",
    [221] = "execve",
    [222] = "mmap",
    [223] = "fadvise64",
    [224] = "swapon",
    [225] = "swapoff",
    [226] = "mprotect",
    [227] = "msync",
    [228] = "mlock",
    [229] = "munlock",
    [230] = "mlockall",
    [231] = "munlockall",
    [232] = "mincore",
    [233] = "madvise",
    [234] = "remap_file_pages",
    [235] = "mbind",
    [236] = "get_mempolicy",
    [237] = "set_mempolicy",
    [238] = "migrate_pages",
    [239] = "move_pages",
    [240] = "rt_tgsigqueueinfo",
    [241] = "perf_event_open",
    [242] = "accept4",
    [243] = "recvmmsg",
    [244] = NULL,
    [245] = NULL,
    [246] = NULL,
    [247] = NULL,
    [248] = NULL,
    [249] = NULL,
    [250] = NULL,
    [251] = NULL,
    [252] = NULL,
    [253] = NULL,
    [254] = NULL,
    [255] = NULL,
    [256] = NULL,
    [257] = NULL,
    [258] = NULL,
    [259] = NULL,
    [260] = "wait4",
    [261] = "prlimit64",
    [262] = "fanotify_init",
    [263] = "fanotify_mark",
    [264] = "name_to_handle_at",
    [265] = "open_by_handle_at",
    [266] = "clock_adjtime",
    [267] = "syncfs",
    [268] = "setns",
    [269] = "sendmmsg",
    [270] = "process_vm_readv",
    [271] = "process_vm_writev",
    [272] = "kcmp",
    [273] = "finit_module",
    [274] = "sched_setattr",
    [275] = "sched_getattr",
    [276] = "renameat2",
    [277] = "seccomp",
    [278] = "getrandom",
    [279] = "memfd_create",
    [280] = "bpf",
    [281] = "execveat",
    [282] = "userfaultfd",
    [283] = "membarrier",
    [284] = "mlock2",
    [285] = "copy_file_range",
    [286] = "preadv2",
    [287] = "pwritev2",
    [288] = "pkey_mprotect",
    [289] = "pkey_alloc",
    [290] = "pkey_free",
    [291] = "statx",
    [292] = NULL,
    [293] = "rseq",
    [294] = NULL,
    [295] = NULL,
    [296] = NULL,
    [297] = NULL,
    [298] = NULL,
    [299] = NULL,
    [300] = NULL,
    [301] = NULL,
    [302] = NULL,
    [303] = NULL,
    [304] = NULL,
    [305] = NULL,
    [306] = NULL,
    [307] = NULL,
    [308] = NULL,
    [309] = NULL,
    [310] = NULL,
    [311] = NULL,
    [312] = NULL,
    [313] = NULL,
    [314] = NULL,
    [315] = NULL,
    [316] = NULL,
    [317] = NULL,
    [318] = NULL,
    [319] = NULL,
    [320] = NULL,
    [321] = NULL,
    [322] = NULL,
    [323] = NULL,
    [324] = NULL,
    [325] = NULL,
    [326] = NULL,
    [327] = NULL,
    [328] = NULL,
    [329] = NULL,
    [330] = NULL,
    [331] = NULL,
    [332] = NULL,
    [333] = NULL,
    [334] = NULL,
    [335] = NULL,
    [336] = NULL,
    [337] = NULL,
    [338] = NULL,
    [339] = NULL,
    [340] = NULL,
    [341] = NULL,
    [342] = NULL,
    [343] = NULL,
    [344] = NULL,
    [345] = NULL,
    [346] = NULL,
    [347] = NULL,
    [348] = NULL,
    [349] = NULL,
    [350] = NULL,
    [351] = NULL,
    [352] = NULL,
    [353] = NULL,
    [354] = NULL,
    [355] = NULL,
    [356] = NULL,
    [357] = NULL,
    [358] = NULL,
    [359] = NULL,
    [360] = NULL,
    [361] = NULL,
    [362] = NULL,
    [363] = NULL,
    [364] = NULL,
    [365] = NULL,
    [366] = NULL,
    [367] = NULL,
    [368] = NULL,
    [369] = NULL,
    [370] = NULL,
    [371] = NULL,
    [372] = NULL,
    [373] = NULL,
    [374] = NULL,
    [375] = NULL,
    [376] = NULL,
    [377] = NULL,
    [378] = NULL,
    [379] = NULL,
    [380] = NULL,
    [381] = NULL,
    [382] = NULL,
    [383] = NULL,
    [384] = NULL,
    [385] = NULL,
    [386] = NULL,
    [387] = NULL,
    [388] = NULL,
    [389] = NULL,
    [390] = NULL,
    [391] = NULL,
    [392] = NULL,
    [393] = NULL,
    [394] = NULL,
    [395] = NULL,
    [396] = NULL,
    [397] = NULL,
    [398] = NULL,
    [399] = NULL,
    [400] = NULL,
    [401] = NULL,
    [402] = NULL,
    [403] = NULL,
    [404] = NULL,
    [405] = NULL,
    [406] = NULL,
    [407] = NULL,
    [408] = NULL,
    [409] = NULL,
    [410] = NULL,
    [411] = NULL,
    [412] = NULL,
    [413] = NULL,
    [414] = NULL,
    [415] = NULL,
    [416] = NULL,
    [417] = NULL,
    [418] = NULL,
    [419] = NULL,
    [420] = NULL,
    [421] = NULL,
    [422] = NULL,
    [423] = NULL,
    [424] = "pidfd_send_signal",
    [425] = "io_uring_setup",
    [426] = "io_uring_enter",
    [427] = "io_uring_register",
    [428] = "open_tree",
    [429] = "move_mount",
    [430] = "fsopen",
    [431] = "fsconfig",
    [432] = "fsmount",
    [433] = "fspick",
    [434] = "pidfd_open",
    [435] = "clone3",
    [436] = "close_range",
    [437] = "openat2",
    [438] = "pidfd_getfd",
    [439] = "faccessat2",
    [440] = "process_madvise",
    [441] = "epoll_pwait2",
    [442] = "mount_setattr",
    [443] = "quotactl_fd",
    [444] = "landlock_create_ruleset",
    [445] = "landlock_add_rule",
    [446] = "landlock_restrict_self",
    [447] = "memfd_secret",
    [448] = "process_mrelease",
    [449] = "futex_waitv",
    [450] = "set_mempolicy_home_node",
    [451] = "cachestat",
    [452] = "fchmodat2",
    [453] = "map_shadow_stack",
    [454] = "futex_wake",
    [455] = "futex_wait",
    [456] = "futex_requeue",
    [457] = "statmount",
    [458] = "listmount",
    [459] = "lsm_get_self_attr",
    [460] = "lsm_set_self_attr",
    [461] = "lsm_list_modules",
    [462] = "mseal",
    [463] = "setxattrat",
    [464] = "getxattrat",
    [465] = "listxattrat",
    [466] = "removexattrat",
};

const char *linux_syscall_name(int sysno) {
    if (sysno >= 0 && sysno <= LINUX_MAX_SYSCALL_NR && g_linux_syscall_names[sysno] != NULL) {
        return g_linux_syscall_names[sysno];
    }
    return "unknown";
}

int64_t syscall_dispatch(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2,
                         uint64_t a3, uint64_t a4, uint64_t a5, void *state, void *neon) {
    ltm_trace("sys_%llu [%s](0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx)",
              (unsigned long long)num, linux_syscall_name((int)num), (unsigned long long)a0, (unsigned long long)a1,
              (unsigned long long)a2, (unsigned long long)a3, (unsigned long long)a4,
              (unsigned long long)a5);

    int64_t ret = -LINUX_ENOSYS;

    switch (num) {
        case LINUX_SYS_read:
            ret = handle_read((int)a0, (void *)a1, (size_t)a2);
            break;
        case LINUX_SYS_write:
            ret = handle_write((int)a0, (const void *)a1, (size_t)a2);
            break;
        case LINUX_SYS_openat:
            ret = handle_openat((int)a0, (const char *)a1, (int)a2, (mode_t)a3);
            break;
        case LINUX_SYS_close:
            ret = handle_close((int)a0);
            break;
        case LINUX_SYS_fstat:
            ret = handle_fstat((int)a0, (struct linux_stat *)a1);
            break;
        case LINUX_SYS_newfstatat:
            ret = handle_newfstatat((int)a0, (const char *)a1, (struct linux_stat *)a2, (int)a3);
            break;
        case LINUX_SYS_statfs:
            ret = handle_statfs((const char *)a0, (struct linux_statfs *)a1);
            break;
        case LINUX_SYS_fstatfs:
            ret = handle_fstatfs((int)a0, (struct linux_statfs *)a1);
            break;
        case LINUX_SYS_truncate:
            ret = handle_truncate((const char *)a0, (off_t)a1);
            break;
        case LINUX_SYS_ftruncate:
            ret = handle_ftruncate((int)a0, (off_t)a1);
            break;
        case LINUX_SYS_lseek:
            ret = handle_lseek((int)a0, (off_t)a1, (int)a2);
            break;
        case LINUX_SYS_ioctl:
            ret = handle_ioctl((int)a0, (unsigned long)a1, (void *)a2);
            break;
        case LINUX_SYS_readv:
            ret = handle_readv((int)a0, (const struct linux_iovec *)a1, (int)a2);
            break;
        case LINUX_SYS_writev:
            ret = handle_writev((int)a0, (const struct linux_iovec *)a1, (int)a2);
            break;
        case LINUX_SYS_dup:
            ret = handle_dup((int)a0);
            break;
        case LINUX_SYS_dup3:
            ret = handle_dup3((int)a0, (int)a1, (int)a2);
            break;
        case LINUX_SYS_fcntl:
            ret = handle_fcntl((int)a0, (int)a1, (unsigned long)a2);
            break;
        case LINUX_SYS_getdents64:
            ret = handle_getdents64((int)a0, (void *)a1, (size_t)a2);
            break;
        case LINUX_SYS_epoll_create1:
            ret = handle_epoll_create1((int)a0);
            break;
        case LINUX_SYS_epoll_ctl:
            ret = handle_epoll_ctl((int)a0, (int)a1, (int)a2, (void *)a3);
            break;
        case LINUX_SYS_epoll_pwait:
            ret = handle_epoll_pwait((int)a0, (void *)a1, (int)a2, (int)a3, (const void *)a4);
            break;
        case LINUX_SYS_ppoll:
            ret = handle_ppoll((struct pollfd *)a0, (nfds_t)a1, (const struct linux_timespec *)a2, (const void *)a3);
            break;
        case LINUX_SYS_pselect6:
            ret = handle_pselect6((int)a0, (fd_set *)a1, (fd_set *)a2, (fd_set *)a3,
                                  (const struct linux_timespec *)a4, (const void *)a5);
            break;
        case LINUX_SYS_tkill:
            ret = handle_tkill((int)a0, (int)a1);
            break;
        case LINUX_SYS_tgkill:
            ret = handle_tgkill((int)a0, (int)a1, (int)a2);
            break;
        case LINUX_SYS_brk:
            ret = (int64_t)handle_brk((uintptr_t)a0);
            break;
        case LINUX_SYS_mmap:
            ret = (int64_t)handle_mmap((void *)a0, (size_t)a1, (int)a2, (int)a3, (int)a4, (off_t)a5);
            break;
        case LINUX_SYS_munmap:
            ret = handle_munmap((void *)a0, (size_t)a1);
            break;
        case LINUX_SYS_mprotect:
            ret = handle_mprotect((void *)a0, (size_t)a1, (int)a2);
            break;
        case LINUX_SYS_madvise:
            ret = handle_madvise((void *)a0, (size_t)a1, (int)a2);
            break;
        case LINUX_SYS_getpid:
            ret = handle_getpid();
            break;
        case LINUX_SYS_getppid:
            ret = handle_getppid();
            break;
        case LINUX_SYS_getuid:
            ret = handle_getuid();
            break;
        case LINUX_SYS_geteuid:
            ret = handle_geteuid();
            break;
        case LINUX_SYS_getgid:
            ret = handle_getgid();
            break;
        case LINUX_SYS_getegid:
            ret = handle_getegid();
            break;
        case LINUX_SYS_getresuid:
            ret = handle_getresuid((uid_t *)a0, (uid_t *)a1, (uid_t *)a2);
            break;
        case LINUX_SYS_getresgid:
            ret = handle_getresgid((gid_t *)a0, (gid_t *)a1, (gid_t *)a2);
            break;
        case LINUX_SYS_getgroups:
            ret = handle_getgroups((int)a0, (gid_t *)a1);
            break;
        case LINUX_SYS_setgroups:
            ret = handle_setgroups((size_t)a0, (const gid_t *)a1);
            break;
        case LINUX_SYS_capget:
            ret = handle_capget((void *)a0, (void *)a1);
            break;
        case LINUX_SYS_capset:
            ret = handle_capset((void *)a0, (const void *)a1);
            break;
        case LINUX_SYS_gettid:
            ret = handle_gettid();
            break;
        case LINUX_SYS_uname:
            ret = handle_uname((struct linux_utsname *)a0);
            break;
        case LINUX_SYS_sysinfo:
            ret = handle_sysinfo((struct linux_sysinfo *)a0);
            break;
        case LINUX_SYS_getitimer:
            ret = handle_getitimer((int)a0, (void *)a1);
            break;
        case LINUX_SYS_setitimer:
            ret = handle_setitimer((int)a0, (const void *)a1, (void *)a2);
            break;
        case LINUX_SYS_clock_gettime:
            ret = handle_clock_gettime((int)a0, (struct linux_timespec *)a1);
            break;
        case LINUX_SYS_gettimeofday:
            ret = handle_gettimeofday((struct linux_timeval *)a0, (struct linux_timezone *)a1);
            break;
        case LINUX_SYS_nanosleep:
            ret = handle_nanosleep((const struct linux_timespec *)a0, (struct linux_timespec *)a1);
            break;
        case LINUX_SYS_exit:
            ret = handle_exit((int)a0);
            break;
        case LINUX_SYS_exit_group:
            ret = handle_exit_group((int)a0);
            break;
        case LINUX_SYS_set_tid_address:
            ret = handle_set_tid_address((int *)a0);
            break;
        case LINUX_SYS_futex:
            ret = handle_futex((uint32_t *)a0, (int)a1, (uint32_t)a2,
                               (const struct linux_timespec *)a3, (uint32_t *)a4, (uint32_t)a5);
            break;
        case LINUX_SYS_clone:
            ret = handle_clone(a0, (void *)a1, (int *)a2, (void *)a3, (int *)a4, state);
            break;
        case LINUX_SYS_readlinkat:
            ret = handle_readlinkat((int)a0, (const char *)a1, (char *)a2, (size_t)a3);
            break;
        case LINUX_SYS_prlimit64:
            ret = handle_prlimit64((int)a0, (int)a1, (const struct linux_rlimit64 *)a2, (struct linux_rlimit64 *)a3);
            break;
        case LINUX_SYS_getrandom:
            ret = handle_getrandom((void *)a0, (size_t)a1, (unsigned int)a2);
            break;
        case LINUX_SYS_rt_sigaction:
            ret = handle_rt_sigaction((int)a0, (const struct linux_sigaction *)a1, (struct linux_sigaction *)a2, (size_t)a3);
            break;
        case LINUX_SYS_rt_sigreturn:
            ret = handle_rt_sigreturn(state, neon);
            break;
        case LINUX_SYS_rt_sigprocmask:
            ret = handle_rt_sigprocmask((int)a0, (const uint64_t *)a1, (uint64_t *)a2, (size_t)a3);
            break;
        case LINUX_SYS_sigaltstack:
            ret = handle_sigaltstack((const void *)a0, (void *)a1);
            break;
        case LINUX_SYS_getcwd:
            ret = handle_getcwd((char *)a0, (size_t)a1);
            break;
        case LINUX_SYS_chdir:
            ret = handle_chdir((const char *)a0);
            break;
        case LINUX_SYS_fchdir:
            ret = handle_fchdir((int)a0);
            break;
        case LINUX_SYS_pipe2:
            ret = handle_pipe2((int *)a0, (int)a1);
            break;
        case LINUX_SYS_wait4:
            ret = handle_wait4((pid_t)a0, (int *)a1, (int)a2, (void *)a3);
            break;
        case LINUX_SYS_execve:
            ret = handle_execve((const char *)a0, (char *const *)a1, (char *const *)a2);
            break;
        case LINUX_SYS_setpgid:
            ret = handle_setpgid((pid_t)a0, (pid_t)a1);
            break;
        case LINUX_SYS_getpgid:
            ret = handle_getpgid((pid_t)a0);
            break;
        case LINUX_SYS_getsid:
            ret = handle_getsid((pid_t)a0);
            break;
        case LINUX_SYS_setsid:
            ret = handle_setsid();
            break;
        case LINUX_SYS_times:
            ret = handle_times((struct linux_tms *)a0);
            break;
        case LINUX_SYS_getrusage:
            ret = handle_getrusage((int)a0, (struct linux_rusage *)a1);
            break;
        case LINUX_SYS_umask:
            ret = handle_umask((mode_t)a0);
            break;
        case LINUX_SYS_setuid:
            ret = handle_setuid((uid_t)a0);
            break;
        case LINUX_SYS_setgid:
            ret = handle_setgid((gid_t)a0);
            break;
        case LINUX_SYS_setreuid:
            ret = handle_setreuid((uid_t)a0, (uid_t)a1);
            break;
        case LINUX_SYS_setregid:
            ret = handle_setregid((gid_t)a0, (gid_t)a1);
            break;
        case LINUX_SYS_setresuid:
            ret = handle_setresuid((uid_t)a0, (uid_t)a1, (uid_t)a2);
            break;
        case LINUX_SYS_setresgid:
            ret = handle_setresgid((gid_t)a0, (gid_t)a1, (gid_t)a2);
            break;
        case LINUX_SYS_unlinkat:
            ret = handle_unlinkat((int)a0, (const char *)a1, (int)a2);
            break;
        case LINUX_SYS_mkdirat:
            ret = handle_mkdirat((int)a0, (const char *)a1, (mode_t)a2);
            break;
        case LINUX_SYS_faccessat:
            ret = handle_faccessat((int)a0, (const char *)a1, (int)a2, (int)a3);
            break;
        case LINUX_SYS_renameat:
            ret = handle_renameat((int)a0, (const char *)a1, (int)a2, (const char *)a3);
            break;
        case LINUX_SYS_renameat2:
            ret = handle_renameat2((int)a0, (const char *)a1, (int)a2, (const char *)a3, (unsigned int)a4);
            break;
        case LINUX_SYS_symlinkat:
            ret = handle_symlinkat((const char *)a0, (int)a1, (const char *)a2);
            break;
        case LINUX_SYS_linkat:
            ret = handle_linkat((int)a0, (const char *)a1, (int)a2, (const char *)a3, (int)a4);
            break;
        case LINUX_SYS_fchmodat:
            ret = handle_fchmodat((int)a0, (const char *)a1, (mode_t)a2, (int)a3);
            break;
        case LINUX_SYS_fchmod:
            ret = handle_fchmod((int)a0, (mode_t)a1);
            break;
        case LINUX_SYS_fchownat:
            ret = handle_fchownat((int)a0, (const char *)a1, (uid_t)a2, (gid_t)a3, (int)a4);
            break;
        case LINUX_SYS_fchown:
            ret = handle_fchown((int)a0, (uid_t)a1, (gid_t)a2);
            break;
        case LINUX_SYS_fsync:
            ret = handle_fsync((int)a0);
            break;
        case LINUX_SYS_fdatasync:
            ret = handle_fdatasync((int)a0);
            break;
        case LINUX_SYS_utimensat:
            ret = handle_utimensat((int)a0, (const char *)a1, (const struct linux_timespec *)a2, (int)a3);
            break;
        case LINUX_SYS_sched_getaffinity:
            ret = handle_sched_getaffinity((pid_t)a0, (size_t)a1, (unsigned long *)a2);
            break;
        case LINUX_SYS_sched_setaffinity:
            ret = handle_sched_setaffinity((pid_t)a0, (size_t)a1, (const unsigned long *)a2);
            break;
        case LINUX_SYS_sched_yield:
            ret = handle_sched_yield();
            break;
        case LINUX_SYS_socket:
            ret = handle_socket((int)a0, (int)a1, (int)a2);
            break;
        case LINUX_SYS_socketpair:
            ret = handle_socketpair((int)a0, (int)a1, (int)a2, (int *)a3);
            break;
        case LINUX_SYS_connect:
            ret = handle_connect((int)a0, (const void *)a1, (socklen_t)a2);
            break;
        case LINUX_SYS_bind:
            ret = handle_bind((int)a0, (const void *)a1, (socklen_t)a2);
            break;
        case LINUX_SYS_listen:
            ret = handle_listen((int)a0, (int)a1);
            break;
        case LINUX_SYS_accept:
            ret = handle_accept((int)a0, (void *)a1, (socklen_t *)a2);
            break;
        case LINUX_SYS_accept4:
            ret = handle_accept4((int)a0, (void *)a1, (socklen_t *)a2, (int)a3);
            break;
        case LINUX_SYS_getsockname:
            ret = handle_getsockname((int)a0, (void *)a1, (socklen_t *)a2);
            break;
        case LINUX_SYS_getpeername:
            ret = handle_getpeername((int)a0, (void *)a1, (socklen_t *)a2);
            break;
        case LINUX_SYS_sendto:
            ret = handle_sendto((int)a0, (const void *)a1, (size_t)a2, (int)a3, (const void *)a4, (socklen_t)a5);
            break;
        case LINUX_SYS_recvfrom:
            ret = handle_recvfrom((int)a0, (void *)a1, (size_t)a2, (int)a3, (void *)a4, (socklen_t *)a5);
            break;
        case LINUX_SYS_sendmsg:
            ret = handle_sendmsg((int)a0, (const void *)a1, (int)a2);
            break;
        case LINUX_SYS_recvmsg:
            ret = handle_recvmsg((int)a0, (void *)a1, (int)a2);
            break;
        case LINUX_SYS_setsockopt:
            ret = handle_setsockopt((int)a0, (int)a1, (int)a2, (const void *)a3, (socklen_t)a4);
            break;
        case LINUX_SYS_getsockopt:
            ret = handle_getsockopt((int)a0, (int)a1, (int)a2, (void *)a3, (socklen_t *)a4);
            break;
        case LINUX_SYS_shutdown:
            ret = handle_shutdown((int)a0, (int)a1);
            break;
        case LINUX_SYS_readahead:
            ret = handle_readahead((int)a0, (off_t)a1, (size_t)a2);
            break;
        case LINUX_SYS_kill:
            ret = handle_kill((pid_t)a0, (int)a1);
            break;
        case LINUX_SYS_flock:
            ret = handle_flock((int)a0, (int)a1);
            break;
        case LINUX_SYS_fallocate:
            ret = handle_fallocate((int)a0, (int)a1, (off_t)a2, (off_t)a3);
            break;
        case LINUX_SYS_chroot:
            ret = handle_chroot((const char *)a0);
            break;
        case LINUX_SYS_pread64:
            ret = handle_pread64((int)a0, (void *)a1, (size_t)a2, (off_t)a3);
            break;
        case LINUX_SYS_pwrite64:
            ret = handle_pwrite64((int)a0, (const void *)a1, (size_t)a2, (off_t)a3);
            break;
        case LINUX_SYS_signalfd4:
            ret = handle_signalfd4((int)a0, (const uint64_t *)a1, (size_t)a2, (int)a3);
            break;
        case LINUX_SYS_sync:
            ret = handle_sync();
            break;
        case LINUX_SYS_syncfs:
            ret = handle_syncfs((int)a0);
            break;
        case LINUX_SYS_timerfd_create:
            ret = handle_timerfd_create((int)a0, (int)a1);
            break;
        case LINUX_SYS_timerfd_settime:
            ret = handle_timerfd_settime((int)a0, (int)a1, (const struct linux_itimerspec *)a2, (struct linux_itimerspec *)a3);
            break;
        case LINUX_SYS_timerfd_gettime:
            ret = handle_timerfd_gettime((int)a0, (struct linux_itimerspec *)a1);
            break;
        case LINUX_SYS_clock_nanosleep:
            ret = handle_clock_nanosleep((int)a0, (int)a1, (const struct linux_timespec *)a2, (struct linux_timespec *)a3);
            break;
        case LINUX_SYS_memfd_create:
            ret = handle_memfd_create((const char *)a0, (unsigned int)a1);
            break;
        case LINUX_SYS_copy_file_range:
            ret = handle_copy_file_range((int)a0, (off_t *)a1, (int)a2, (off_t *)a3, (size_t)a4, (unsigned int)a5);
            break;
        case LINUX_SYS_close_range:
            ret = handle_close_range((unsigned int)a0, (unsigned int)a1, (unsigned int)a2);
            break;
        case LINUX_SYS_openat2:
            ret = handle_openat2((int)a0, (const char *)a1, (struct linux_open_how *)a2, (size_t)a3);
            break;
        case LINUX_SYS_faccessat2:
            ret = handle_faccessat2((int)a0, (const char *)a1, (int)a2, (int)a3);
            break;
        case LINUX_SYS_rseq:
            ret = -LINUX_ENOSYS;
            break;

        /* Wave 3 Syscalls */
        case LINUX_SYS_setxattr:
            ret = handle_setxattr((const char *)a0, (const char *)a1, (const void *)a2, (size_t)a3, (int)a4);
            break;
        case LINUX_SYS_lsetxattr:
            ret = handle_lsetxattr((const char *)a0, (const char *)a1, (const void *)a2, (size_t)a3, (int)a4);
            break;
        case LINUX_SYS_fsetxattr:
            ret = handle_fsetxattr((int)a0, (const char *)a1, (const void *)a2, (size_t)a3, (int)a4);
            break;
        case LINUX_SYS_getxattr:
            ret = handle_getxattr((const char *)a0, (const char *)a1, (void *)a2, (size_t)a3);
            break;
        case LINUX_SYS_lgetxattr:
            ret = handle_lgetxattr((const char *)a0, (const char *)a1, (void *)a2, (size_t)a3);
            break;
        case LINUX_SYS_fgetxattr:
            ret = handle_fgetxattr((int)a0, (const char *)a1, (void *)a2, (size_t)a3);
            break;
        case LINUX_SYS_listxattr:
            ret = handle_listxattr((const char *)a0, (char *)a1, (size_t)a2);
            break;
        case LINUX_SYS_llistxattr:
            ret = handle_llistxattr((const char *)a0, (char *)a1, (size_t)a2);
            break;
        case LINUX_SYS_flistxattr:
            ret = handle_flistxattr((int)a0, (char *)a1, (size_t)a2);
            break;
        case LINUX_SYS_removexattr:
            ret = handle_removexattr((const char *)a0, (const char *)a1);
            break;
        case LINUX_SYS_lremovexattr:
            ret = handle_lremovexattr((const char *)a0, (const char *)a1);
            break;
        case LINUX_SYS_fremovexattr:
            ret = handle_fremovexattr((int)a0, (const char *)a1);
            break;
        case LINUX_SYS_eventfd2:
            ret = handle_eventfd2((unsigned int)a0, (int)a1);
            break;
        case LINUX_SYS_preadv:
            ret = handle_preadv((int)a0, (const struct linux_iovec *)a1, (int)a2, (off_t)a3);
            break;
        case LINUX_SYS_pwritev:
            ret = handle_pwritev((int)a0, (const struct linux_iovec *)a1, (int)a2, (off_t)a3);
            break;
        case LINUX_SYS_sendfile:
            ret = handle_sendfile((int)a0, (int)a1, (off_t *)a2, (size_t)a3);
            break;
        case LINUX_SYS_splice:
            ret = handle_splice((int)a0, (off_t *)a1, (int)a2, (off_t *)a3, (size_t)a4, (unsigned int)a5);
            break;
        case LINUX_SYS_tee:
            ret = handle_tee((int)a0, (int)a1, (size_t)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_sync_file_range:
            ret = handle_sync_file_range((int)a0, (off_t)a1, (off_t)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_personality:
            ret = handle_personality((unsigned long)a0);
            break;
        case LINUX_SYS_clock_getres:
            ret = handle_clock_getres((int)a0, (struct linux_timespec *)a1);
            break;
        case LINUX_SYS_getrlimit:
            ret = handle_getrlimit((int)a0, (struct linux_rlimit *)a1);
            break;
        case LINUX_SYS_setrlimit:
            ret = handle_setrlimit((int)a0, (const struct linux_rlimit *)a1);
            break;
        case LINUX_SYS_prctl:
            ret = handle_prctl((int)a0, (unsigned long)a1, (unsigned long)a2, (unsigned long)a3, (unsigned long)a4);
            break;
        case LINUX_SYS_getcpu:
            ret = handle_getcpu((unsigned *)a0, (unsigned *)a1, (void *)a2);
            break;

        /* Wave 4 Syscalls */
        case LINUX_SYS_mknodat:
            ret = handle_mknodat((int)a0, (const char *)a1, (mode_t)a2, (dev_t)a3);
            break;
        case LINUX_SYS_vhangup:
            ret = handle_vhangup();
            break;
        case LINUX_SYS_waitid:
            ret = handle_waitid((int)a0, (id_t)a1, (struct linux_siginfo *)a2, (int)a3, (void *)a4);
            break;
        case LINUX_SYS_sched_setparam:
            ret = handle_sched_setparam((pid_t)a0, (const struct linux_sched_param *)a1);
            break;
        case LINUX_SYS_sched_getparam:
            ret = handle_sched_getparam((pid_t)a0, (struct linux_sched_param *)a1);
            break;
        case LINUX_SYS_sched_setscheduler:
            ret = handle_sched_setscheduler((pid_t)a0, (int)a1, (const struct linux_sched_param *)a2);
            break;
        case LINUX_SYS_sched_getscheduler:
            ret = handle_sched_getscheduler((pid_t)a0);
            break;
        case LINUX_SYS_sched_get_priority_max:
            ret = handle_sched_get_priority_max((int)a0);
            break;
        case LINUX_SYS_sched_get_priority_min:
            ret = handle_sched_get_priority_min((int)a0);
            break;
        case LINUX_SYS_sched_rr_get_interval:
            ret = handle_sched_rr_get_interval((pid_t)a0, (struct linux_timespec *)a1);
            break;
        case LINUX_SYS_rt_sigpending:
            ret = handle_rt_sigpending((void *)a0, (size_t)a1);
            break;
        case LINUX_SYS_setpriority:
            ret = handle_setpriority((int)a0, (int)a1, (int)a2);
            break;
        case LINUX_SYS_getpriority:
            ret = handle_getpriority((int)a0, (int)a1);
            break;
        case LINUX_SYS_setfsuid:
            ret = handle_setfsuid((uid_t)a0);
            break;
        case LINUX_SYS_setfsgid:
            ret = handle_setfsgid((gid_t)a0);
            break;
        case LINUX_SYS_sethostname:
            ret = handle_sethostname((const char *)a0, (size_t)a1);
            break;
        case LINUX_SYS_setdomainname:
            ret = handle_setdomainname((const char *)a0, (size_t)a1);
            break;
        case LINUX_SYS_mremap:
            ret = handle_mremap((unsigned long)a0, (unsigned long)a1, (unsigned long)a2, (unsigned long)a3, (unsigned long)a4);
            break;
        case LINUX_SYS_fadvise64:
            ret = handle_fadvise64((int)a0, (off_t)a1, (off_t)a2, (int)a3);
            break;
        case LINUX_SYS_msync:
            ret = handle_msync((unsigned long)a0, (size_t)a1, (int)a2);
            break;
        case LINUX_SYS_mlock:
            ret = handle_mlock((unsigned long)a0, (size_t)a1);
            break;
        case LINUX_SYS_munlock:
            ret = handle_munlock((unsigned long)a0, (size_t)a1);
            break;
        case LINUX_SYS_mlockall:
            ret = handle_mlockall((int)a0);
            break;
        case LINUX_SYS_munlockall:
            ret = handle_munlockall();
            break;
        case LINUX_SYS_mincore:
            ret = handle_mincore((unsigned long)a0, (size_t)a1, (unsigned char *)a2);
            break;

        /* Wave 5 Syscalls */
        case LINUX_SYS_inotify_init1:
            ret = handle_inotify_init1((int)a0);
            break;
        case LINUX_SYS_inotify_add_watch:
            ret = handle_inotify_add_watch((int)a0, (const char *)a1, (uint32_t)a2);
            break;
        case LINUX_SYS_inotify_rm_watch:
            ret = handle_inotify_rm_watch((int)a0, (int)a1);
            break;
        case LINUX_SYS_ioprio_set:
            ret = handle_ioprio_set((int)a0, (int)a1, (int)a2);
            break;
        case LINUX_SYS_ioprio_get:
            ret = handle_ioprio_get((int)a0, (int)a1);
            break;
        case LINUX_SYS_timer_create:
            ret = handle_timer_create((int)a0, (void *)a1, (int *)a2);
            break;
        case LINUX_SYS_timer_gettime:
            ret = handle_timer_gettime((int)a0, (struct linux_itimerspec *)a1);
            break;
        case LINUX_SYS_timer_getoverrun:
            ret = handle_timer_getoverrun((int)a0);
            break;
        case LINUX_SYS_timer_settime:
            ret = handle_timer_settime((int)a0, (int)a1, (const struct linux_itimerspec *)a2, (struct linux_itimerspec *)a3);
            break;
        case LINUX_SYS_timer_delete:
            ret = handle_timer_delete((int)a0);
            break;
        case LINUX_SYS_rt_sigsuspend:
            ret = handle_rt_sigsuspend((const uint64_t *)a0, (size_t)a1);
            break;
        case LINUX_SYS_rt_sigtimedwait:
            ret = handle_rt_sigtimedwait((const uint64_t *)a0, (struct linux_siginfo *)a1, (const struct linux_timespec *)a2, (size_t)a3);
            break;
        case LINUX_SYS_rt_sigqueueinfo:
            ret = handle_rt_sigqueueinfo((pid_t)a0, (int)a1, (struct linux_siginfo *)a2);
            break;
        case LINUX_SYS_msgget:
            ret = handle_msgget((key_t)a0, (int)a1);
            break;
        case LINUX_SYS_msgctl:
            ret = handle_msgctl((int)a0, (int)a1, (void *)a2);
            break;
        case LINUX_SYS_msgrcv:
            ret = handle_msgrcv((int)a0, (void *)a1, (size_t)a2, (long)a3, (int)a4);
            break;
        case LINUX_SYS_msgsnd:
            ret = handle_msgsnd((int)a0, (const void *)a1, (size_t)a2, (int)a3);
            break;
        case LINUX_SYS_semget:
            ret = handle_semget((key_t)a0, (int)a1, (int)a2);
            break;
        case LINUX_SYS_semctl:
            ret = handle_semctl((int)a0, (int)a1, (int)a2, (unsigned long)a3);
            break;
        case LINUX_SYS_semop:
            ret = handle_semop((int)a0, (void *)a1, (size_t)a2);
            break;
        case LINUX_SYS_semtimedop:
            ret = handle_semtimedop((int)a0, (void *)a1, (size_t)a2, (const struct linux_timespec *)a3);
            break;
        case LINUX_SYS_execveat:
            ret = handle_execveat((int)a0, (const char *)a1, (char *const *)a2, (char *const *)a3, (int)a4);
            break;
        case LINUX_SYS_preadv2:
            ret = handle_preadv2((int)a0, (const struct linux_iovec *)a1, (int)a2, (off_t)a3, (int)a4);
            break;
        case LINUX_SYS_pwritev2:
            ret = handle_pwritev2((int)a0, (const struct linux_iovec *)a1, (int)a2, (off_t)a3, (int)a4);
            break;
        case LINUX_SYS_statx:
            ret = handle_statx((int)a0, (const char *)a1, (int)a2, (unsigned int)a3, (struct linux_statx *)a4);
            break;
        case LINUX_SYS_pidfd_open:
            ret = handle_pidfd_open((pid_t)a0, (unsigned int)a1);
            break;
        case LINUX_SYS_shmget:
            ret = handle_shmget((key_t)a0, (size_t)a1, (int)a2);
            break;
        case LINUX_SYS_shmctl:
            ret = handle_shmctl((int)a0, (int)a1, (void *)a2);
            break;
        case LINUX_SYS_shmat:
            ret = handle_shmat((int)a0, (const void *)a1, (int)a2);
            break;
        case LINUX_SYS_shmdt:
            ret = handle_shmdt((const void *)a0);
            break;
        case LINUX_SYS_mq_open:
            ret = handle_mq_open((const char *)a0, (int)a1, (mode_t)a2, (struct linux_mq_attr *)a3);
            break;
        case LINUX_SYS_mq_unlink:
            ret = handle_mq_unlink((const char *)a0);
            break;
        case LINUX_SYS_mq_timedsend:
            ret = handle_mq_timedsend((int)a0, (const char *)a1, (size_t)a2, (unsigned int)a3, (const struct linux_timespec *)a4);
            break;
        case LINUX_SYS_mq_timedreceive:
            ret = handle_mq_timedreceive((int)a0, (char *)a1, (size_t)a2, (unsigned int *)a3, (const struct linux_timespec *)a4);
            break;
        case LINUX_SYS_mq_notify:
            ret = handle_mq_notify((int)a0, (const void *)a1);
            break;
        case LINUX_SYS_mq_getsetattr:
            ret = handle_mq_getsetattr((int)a0, (const struct linux_mq_attr *)a1, (struct linux_mq_attr *)a2);
            break;
        case LINUX_SYS_sendmmsg:
            ret = handle_sendmmsg((int)a0, (struct linux_mmsghdr *)a1, (unsigned int)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_recvmmsg:
            ret = handle_recvmmsg((int)a0, (struct linux_mmsghdr *)a1, (unsigned int)a2, (unsigned int)a3, (struct linux_timespec *)a4);
            break;
        case LINUX_SYS_clock_settime:
            ret = handle_clock_settime((clockid_t)a0, (const struct linux_timespec *)a1);
            break;
        case LINUX_SYS_settimeofday:
            ret = handle_settimeofday((const struct timeval *)a0, (const struct timezone *)a1);
            break;
        case LINUX_SYS_adjtimex:
            ret = handle_adjtimex((struct linux_timex *)a0);
            break;
        case LINUX_SYS_clock_adjtime:
            ret = handle_clock_adjtime((clockid_t)a0, (struct linux_timex *)a1);
            break;
        case LINUX_SYS_mlock2:
            ret = handle_mlock2((unsigned long)a0, (size_t)a1, (int)a2);
            break;
        case LINUX_SYS_membarrier:
            ret = handle_membarrier((int)a0, (unsigned int)a1, (int)a2);
            break;
        case LINUX_SYS_userfaultfd:
            ret = handle_userfaultfd((int)a0);
            break;
        case LINUX_SYS_sched_setattr:
            ret = handle_sched_setattr((pid_t)a0, (void *)a1, (unsigned int)a2);
            break;
        case LINUX_SYS_sched_getattr:
            ret = handle_sched_getattr((pid_t)a0, (void *)a1, (unsigned int)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_rt_tgsigqueueinfo:
            ret = handle_rt_tgsigqueueinfo((pid_t)a0, (pid_t)a1, (int)a2, (struct linux_siginfo *)a3);
            break;
        case LINUX_SYS_pidfd_send_signal:
            ret = handle_pidfd_send_signal((int)a0, (int)a1, (struct linux_siginfo *)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_pidfd_getfd:
            ret = handle_pidfd_getfd((int)a0, (int)a1, (unsigned int)a2);
            break;

        /* Wave 7 Syscalls */
        case LINUX_SYS_io_setup:
            ret = handle_io_setup((unsigned)a0, (linux_aio_context_t *)a1);
            break;
        case LINUX_SYS_io_destroy:
            ret = handle_io_destroy((linux_aio_context_t)a0);
            break;
        case LINUX_SYS_io_submit:
            ret = handle_io_submit((linux_aio_context_t)a0, (long)a1, (struct linux_iocb **)a2);
            break;
        case LINUX_SYS_io_cancel:
            ret = handle_io_cancel((linux_aio_context_t)a0, (struct linux_iocb *)a1, (struct linux_io_event *)a2);
            break;
        case LINUX_SYS_io_getevents:
            ret = handle_io_getevents((linux_aio_context_t)a0, (long)a1, (long)a2, (struct linux_io_event *)a3, (struct linux_timespec *)a4);
            break;
        case LINUX_SYS_lookup_dcookie:
            ret = handle_lookup_dcookie((uint64_t)a0, (char *)a1, (size_t)a2);
            break;
        case LINUX_SYS_umount2:
            ret = handle_umount2((const char *)a0, (int)a1);
            break;
        case LINUX_SYS_mount:
            ret = handle_mount((const char *)a0, (const char *)a1, (const char *)a2, (unsigned long)a3, (const void *)a4);
            break;
        case LINUX_SYS_pivot_root:
            ret = handle_pivot_root((const char *)a0, (const char *)a1);
            break;
        case LINUX_SYS_nfsservctl:
            ret = handle_nfsservctl((int)a0, (void *)a1, (void *)a2);
            break;
        case LINUX_SYS_quotactl:
            ret = handle_quotactl((int)a0, (const char *)a1, (int)a2, (void *)a3);
            break;
        case LINUX_SYS_vmsplice:
            ret = handle_vmsplice((int)a0, (const struct iovec *)a1, (unsigned long)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_acct:
            ret = handle_acct((const char *)a0);
            break;
        case LINUX_SYS_unshare:
            ret = handle_unshare((int)a0);
            break;
        case LINUX_SYS_set_robust_list:
            ret = handle_set_robust_list((struct linux_robust_list_head *)a0, (size_t)a1);
            break;
        case LINUX_SYS_get_robust_list:
            ret = handle_get_robust_list((int)a0, (struct linux_robust_list_head **)a1, (size_t *)a2);
            break;
        case LINUX_SYS_kexec_load:
            ret = handle_kexec_load((unsigned long)a0, (unsigned long)a1, (struct linux_kexec_segment *)a2, (unsigned long)a3);
            break;
        case LINUX_SYS_init_module:
            ret = handle_init_module((void *)a0, (unsigned long)a1, (const char *)a2);
            break;
        case LINUX_SYS_delete_module:
            ret = handle_delete_module((const char *)a0, (unsigned int)a1);
            break;
        case LINUX_SYS_syslog:
            ret = handle_syslog((int)a0, (char *)a1, (int)a2);
            break;
        case LINUX_SYS_ptrace:
            ret = handle_ptrace((long)a0, (pid_t)a1, (void *)a2, (void *)a3);
            break;
        case LINUX_SYS_restart_syscall:
            ret = handle_restart_syscall();
            break;
        case LINUX_SYS_reboot:
            ret = handle_reboot((int)a0, (int)a1, (int)a2, (void *)a3);
            break;
        case LINUX_SYS_add_key:
            ret = handle_add_key((const char *)a0, (const char *)a1, (const void *)a2, (size_t)a3, (int32_t)a4);
            break;
        case LINUX_SYS_request_key:
            ret = handle_request_key((const char *)a0, (const char *)a1, (const char *)a2, (int32_t)a3);
            break;

        /* Wave 8 Syscalls */
        case LINUX_SYS_keyctl:
            ret = handle_keyctl((int)a0, (unsigned long)a1, (unsigned long)a2, (unsigned long)a3, (unsigned long)a4);
            break;
        case LINUX_SYS_swapon:
            ret = handle_swapon((const char *)a0, (int)a1);
            break;
        case LINUX_SYS_swapoff:
            ret = handle_swapoff((const char *)a0);
            break;
        case LINUX_SYS_remap_file_pages:
            ret = handle_remap_file_pages((void *)a0, (size_t)a1, (int)a2, (size_t)a3, (int)a4);
            break;
        case LINUX_SYS_mbind:
            ret = handle_mbind((void *)a0, (unsigned long)a1, (int)a2, (const unsigned long *)a3, (unsigned long)a4, (unsigned)a5);
            break;
        case LINUX_SYS_get_mempolicy:
            ret = handle_get_mempolicy((int *)a0, (unsigned long *)a1, (unsigned long)a2, (void *)a3, (unsigned long)a4);
            break;
        case LINUX_SYS_set_mempolicy:
            ret = handle_set_mempolicy((int)a0, (const unsigned long *)a1, (unsigned long)a2);
            break;
        case LINUX_SYS_migrate_pages:
            ret = handle_migrate_pages((pid_t)a0, (unsigned long)a1, (const unsigned long *)a2, (const unsigned long *)a3);
            break;
        case LINUX_SYS_move_pages:
            ret = handle_move_pages((pid_t)a0, (unsigned long)a1, (const void **)a2, (const int *)a3, (int *)a4, (int)a5);
            break;
        case LINUX_SYS_perf_event_open:
            ret = handle_perf_event_open((void *)a0, (pid_t)a1, (int)a2, (int)a3, (unsigned long)a4);
            break;
        case LINUX_SYS_fanotify_init:
            ret = handle_fanotify_init((unsigned int)a0, (unsigned int)a1);
            break;
        case LINUX_SYS_fanotify_mark:
            ret = handle_fanotify_mark((int)a0, (unsigned int)a1, (uint64_t)a2, (int)a3, (const char *)a4);
            break;
        case LINUX_SYS_name_to_handle_at:
            ret = handle_name_to_handle_at((int)a0, (const char *)a1, (struct linux_file_handle *)a2, (int *)a3, (int)a4);
            break;
        case LINUX_SYS_open_by_handle_at:
            ret = handle_open_by_handle_at((int)a0, (struct linux_file_handle *)a1, (int)a2);
            break;
        case LINUX_SYS_setns:
            ret = handle_setns((int)a0, (int)a1);
            break;
        case LINUX_SYS_process_vm_readv:
            ret = handle_process_vm_readv((pid_t)a0, (const struct iovec *)a1, (unsigned long)a2, (const struct iovec *)a3, (unsigned long)a4, (unsigned long)a5);
            break;
        case LINUX_SYS_process_vm_writev:
            ret = handle_process_vm_writev((pid_t)a0, (const struct iovec *)a1, (unsigned long)a2, (const struct iovec *)a3, (unsigned long)a4, (unsigned long)a5);
            break;
        case LINUX_SYS_kcmp:
            ret = handle_kcmp((pid_t)a0, (pid_t)a1, (int)a2, (unsigned long)a3, (unsigned long)a4);
            break;
        case LINUX_SYS_finit_module:
            ret = handle_finit_module((int)a0, (const char *)a1, (int)a2);
            break;
        case LINUX_SYS_seccomp:
            ret = handle_seccomp((unsigned int)a0, (unsigned int)a1, (void *)a2);
            break;
        case LINUX_SYS_bpf:
            ret = handle_bpf((int)a0, (void *)a1, (unsigned int)a2);
            break;
        case LINUX_SYS_pkey_mprotect:
            ret = handle_pkey_mprotect((void *)a0, (size_t)a1, (int)a2, (int)a3);
            break;
        case LINUX_SYS_pkey_alloc:
            ret = handle_pkey_alloc((unsigned int)a0, (unsigned int)a1);
            break;
        case LINUX_SYS_pkey_free:
            ret = handle_pkey_free((int)a0);
            break;
        case LINUX_SYS_clone3:
            ret = handle_clone3((struct linux_clone_args *)a0, (size_t)a1, state);
            break;
        case LINUX_SYS_io_uring_setup:
            ret = handle_io_uring_setup((uint32_t)a0, (void *)a1);
            break;
        case LINUX_SYS_io_uring_enter:
            ret = handle_io_uring_enter((unsigned int)a0, (unsigned int)a1, (unsigned int)a2, (unsigned int)a3, (void *)a4);
            break;
        case LINUX_SYS_io_uring_register:
            ret = handle_io_uring_register((unsigned int)a0, (unsigned int)a1, (void *)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_open_tree:
            ret = handle_open_tree((int)a0, (const char *)a1, (unsigned int)a2);
            break;
        case LINUX_SYS_move_mount:
            ret = handle_move_mount((int)a0, (const char *)a1, (int)a2, (const char *)a3, (unsigned int)a4);
            break;
        case LINUX_SYS_fsopen:
            ret = handle_fsopen((const char *)a0, (unsigned int)a1);
            break;
        case LINUX_SYS_fsconfig:
            ret = handle_fsconfig((int)a0, (unsigned int)a1, (const char *)a2, (const void *)a3, (int)a4);
            break;
        case LINUX_SYS_fsmount:
            ret = handle_fsmount((int)a0, (unsigned int)a1, (unsigned int)a2);
            break;
        case LINUX_SYS_fspick:
            ret = handle_fspick((int)a0, (const char *)a1, (unsigned int)a2);
            break;
        case LINUX_SYS_process_madvise:
            ret = handle_process_madvise((int)a0, (const struct iovec *)a1, (size_t)a2, (int)a3, (unsigned int)a4);
            break;
        case LINUX_SYS_epoll_pwait2:
            ret = handle_epoll_pwait2((int)a0, (struct linux_epoll_event *)a1, (int)a2, (const struct timespec *)a3, (const void *)a4);
            break;
        case LINUX_SYS_mount_setattr:
            ret = handle_mount_setattr((int)a0, (const char *)a1, (unsigned int)a2, (void *)a3, (size_t)a4);
            break;
        case LINUX_SYS_quotactl_fd:
            ret = handle_quotactl_fd((int)a0, (unsigned int)a1, (int)a2, (void *)a3);
            break;
        case LINUX_SYS_landlock_create_ruleset:
            ret = handle_landlock_create_ruleset((const void *)a0, (size_t)a1, (uint32_t)a2);
            break;
        case LINUX_SYS_landlock_add_rule:
            ret = handle_landlock_add_rule((int)a0, (int)a1, (const void *)a2, (uint32_t)a3);
            break;
        case LINUX_SYS_landlock_restrict_self:
            ret = handle_landlock_restrict_self((int)a0, (uint32_t)a1);
            break;
        case LINUX_SYS_memfd_secret:
            ret = handle_memfd_secret((unsigned int)a0);
            break;
        case LINUX_SYS_process_mrelease:
            ret = handle_process_mrelease((int)a0, (unsigned int)a1);
            break;
        case LINUX_SYS_futex_waitv:
            ret = handle_futex_waitv((struct linux_futex_waitv *)a0, (unsigned int)a1, (unsigned int)a2, (struct timespec *)a3, (clockid_t)a4);
            break;
        case LINUX_SYS_set_mempolicy_home_node:
            ret = handle_set_mempolicy_home_node((unsigned long)a0, (unsigned long)a1, (unsigned long)a2, (unsigned long)a3);
            break;
        case LINUX_SYS_cachestat:
            ret = handle_cachestat((unsigned int)a0, (struct linux_cachestat_range *)a1, (struct linux_cachestat *)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_fchmodat2:
            ret = handle_fchmodat2((int)a0, (const char *)a1, (mode_t)a2, (int)a3);
            break;
        case LINUX_SYS_map_shadow_stack:
            ret = handle_map_shadow_stack((unsigned long)a0, (unsigned long)a1, (unsigned int)a2);
            break;
        case LINUX_SYS_futex_wake:
            ret = handle_futex_wake((volatile uint32_t *)a0, (unsigned long)a1, (int)a2);
            break;
        case LINUX_SYS_futex_wait:
            ret = handle_futex_wait((volatile uint32_t *)a0, (unsigned long)a1, (uint32_t)a2, (struct timespec *)a3);
            break;
        case LINUX_SYS_futex_requeue:
            ret = handle_futex_requeue((volatile uint32_t *)a0, (unsigned long)a1, (int)a2, (volatile uint32_t *)a3, (int)a4);
            break;
        case LINUX_SYS_statmount:
            ret = handle_statmount((const void *)a0, (void *)a1, (size_t)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_listmount:
            ret = handle_listmount((const void *)a0, (uint64_t *)a1, (size_t)a2, (unsigned int)a3);
            break;
        case LINUX_SYS_lsm_get_self_attr:
            ret = handle_lsm_get_self_attr((unsigned int)a0, (void *)a1, (size_t *)a2, (uint32_t)a3);
            break;
        case LINUX_SYS_lsm_set_self_attr:
            ret = handle_lsm_set_self_attr((unsigned int)a0, (const void *)a1, (size_t)a2, (uint32_t)a3);
            break;
        case LINUX_SYS_lsm_list_modules:
            ret = handle_lsm_list_modules((uint64_t *)a0, (size_t *)a1, (uint32_t)a2);
            break;
        case LINUX_SYS_mseal:
            ret = handle_mseal((unsigned long)a0, (size_t)a1, (unsigned long)a2);
            break;
        case LINUX_SYS_setxattrat:
            ret = handle_setxattrat((int)a0, (const char *)a1, (unsigned int)a2, (const char *)a3, (const void *)a4, (size_t)a5, 0);
            break;
        case LINUX_SYS_getxattrat:
            ret = handle_getxattrat((int)a0, (const char *)a1, (unsigned int)a2, (const char *)a3, (void *)a4, (size_t)a5);
            break;
        case LINUX_SYS_listxattrat:
            ret = handle_listxattrat((int)a0, (const char *)a1, (unsigned int)a2, (char *)a3, (size_t)a4);
            break;
        case LINUX_SYS_removexattrat:
            ret = handle_removexattrat((int)a0, (const char *)a1, (unsigned int)a2, (const char *)a3);
            break;

        default:
            ltm_log_debug("Unhandled Linux syscall: %llu (%s)", (unsigned long long)num, linux_syscall_name((int)num));
            ret = -LINUX_ENOSYS;
            break;
    }

    ltm_trace("syscall %llu [%s](0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx, 0x%llx) = %lld",
              (unsigned long long)num, linux_syscall_name((int)num),
              (unsigned long long)a0, (unsigned long long)a1, (unsigned long long)a2,
              (unsigned long long)a3, (unsigned long long)a4, (unsigned long long)a5,
              (long long)ret);

    return ret;
}
