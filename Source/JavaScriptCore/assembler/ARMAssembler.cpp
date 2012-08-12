/*
 * Copyright (C) 2009 University of Szeged
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY UNIVERSITY OF SZEGED ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL UNIVERSITY OF SZEGED OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"

#if ENABLE(ASSEMBLER) && CPU(ARM_TRADITIONAL)

#include "ARMAssembler.h"

namespace JSC {

// Patching helpers

void ARMAssembler::patchConstantPoolLoad(void* loadAddr, void* constPoolAddr)
{
    ARMWord *ldr = reinterpret_cast<ARMWord*>(loadAddr);
    ARMWord diff = reinterpret_cast<ARMWord*>(constPoolAddr) - ldr;
    ARMWord index = (*ldr & 0xfff) >> 1;

    ASSERT(diff >= 1);
    if (diff >= 2 || index > 0) {
        diff = (diff + index - 2) * sizeof(ARMWord);
        ASSERT(diff <= 0xfff);
        *ldr = (*ldr & ~0xfff) | diff;
    } else
        *ldr = (*ldr & ~(0xfff | ARMAssembler::DataTransferUp)) | sizeof(ARMWord);
}

// Handle immediates

ARMWord ARMAssembler::getOp2(ARMWord imm)
{
    int rol;

    if (imm <= 0xff)
        return Op2Immediate | imm;

    if ((imm & 0xff000000) == 0) {
        imm <<= 8;
        rol = 8;
    }
    else {
        imm = (imm << 24) | (imm >> 8);
        rol = 0;
    }

    if ((imm & 0xff000000) == 0) {
        imm <<= 8;
        rol += 4;
    }

    if ((imm & 0xf0000000) == 0) {
        imm <<= 4;
        rol += 2;
    }

    if ((imm & 0xc0000000) == 0) {
        imm <<= 2;
        rol += 1;
    }

    if ((imm & 0x00ffffff) == 0)
        return Op2Immediate | (imm >> 24) | (rol << 8);

    return InvalidImmediate;
}

int ARMAssembler::genInt(int reg, ARMWord imm, bool positive)
{
    // Step1: Search a non-immediate part
    ARMWord mask;
    ARMWord imm1;
    ARMWord imm2;
    int rol;

    mask = 0xff000000;
    rol = 8;
    while(1) {
        if ((imm & mask) == 0) {
            imm = (imm << rol) | (imm >> (32 - rol));
            rol = 4 + (rol >> 1);
            break;
        }
        rol += 2;
        mask >>= 2;
        if (mask & 0x3) {
            // rol 8
            imm = (imm << 8) | (imm >> 24);
            mask = 0xff00;
            rol = 24;
            while (1) {
                if ((imm & mask) == 0) {
                    imm = (imm << rol) | (imm >> (32 - rol));
                    rol = (rol >> 1) - 8;
                    break;
                }
                rol += 2;
                mask >>= 2;
                if (mask & 0x3)
                    return 0;
            }
            break;
        }
    }

    ASSERT((imm & 0xff) == 0);

    if ((imm & 0xff000000) == 0) {
        imm1 = Op2Immediate | ((imm >> 16) & 0xff) | (((rol + 4) & 0xf) << 8);
        imm2 = Op2Immediate | ((imm >> 8) & 0xff) | (((rol + 8) & 0xf) << 8);
    } else if (imm & 0xc0000000) {
        imm1 = Op2Immediate | ((imm >> 24) & 0xff) | ((rol & 0xf) << 8);
        imm <<= 8;
        rol += 4;

        if ((imm & 0xff000000) == 0) {
            imm <<= 8;
            rol += 4;
        }

        if ((imm & 0xf0000000) == 0) {
            imm <<= 4;
            rol += 2;
        }

        if ((imm & 0xc0000000) == 0) {
            imm <<= 2;
            rol += 1;
        }

        if ((imm & 0x00ffffff) == 0)
            imm2 = Op2Immediate | (imm >> 24) | ((rol & 0xf) << 8);
        else
            return 0;
    } else {
        if ((imm & 0xf0000000) == 0) {
            imm <<= 4;
            rol += 2;
        }

        if ((imm & 0xc0000000) == 0) {
            imm <<= 2;
            rol += 1;
        }

        imm1 = Op2Immediate | ((imm >> 24) & 0xff) | ((rol & 0xf) << 8);
        imm <<= 8;
        rol += 4;

        if ((imm & 0xf0000000) == 0) {
            imm <<= 4;
            rol += 2;
        }

        if ((imm & 0xc0000000) == 0) {
            imm <<= 2;
            rol += 1;
        }

        if ((imm & 0x00ffffff) == 0)
            imm2 = Op2Immediate | (imm >> 24) | ((rol & 0xf) << 8);
        else
            return 0;
    }

    if (positive) {
        mov_r(reg, imm1);
        orr_r(reg, reg, imm2);
    } else {
        mvn_r(reg, imm1);
        bic_r(reg, reg, imm2);
    }

    return 1;
}

ARMWord ARMAssembler::getImm(ARMWord imm, int tmpReg, bool invert)
{
    ARMWord tmp;

    // Do it by 1 instruction
    tmp = getOp2(imm);
    if (tmp != InvalidImmediate)
        return tmp;

    tmp = getOp2(~imm);
    if (tmp != InvalidImmediate) {
        if (invert)
            return tmp | Op2InvertedImmediate;
        mvn_r(tmpReg, tmp);
        return tmpReg;
    }

    return encodeComplexImm(imm, tmpReg);
}

void ARMAssembler::moveImm(ARMWord imm, int dest)
{
    ARMWord tmp;

    // Do it by 1 instruction
    tmp = getOp2(imm);
    if (tmp != InvalidImmediate) {
        mov_r(dest, tmp);
        return;
    }

    tmp = getOp2(~imm);
    if (tmp != InvalidImmediate) {
        mvn_r(dest, tmp);
        return;
    }

    encodeComplexImm(imm, dest);
}

ARMWord ARMAssembler::encodeComplexImm(ARMWord imm, int dest)
{
#if WTF_ARM_ARCH_AT_LEAST(7)
    ARMWord tmp = getImm16Op2(imm);
    if (tmp != InvalidImmediate) {
        movw_r(dest, tmp);
        return dest;
    }
    movw_r(dest, getImm16Op2(imm & 0xffff));
    movt_r(dest, getImm16Op2(imm >> 16));
    return dest;
#else
    // Do it by 2 instruction
    if (genInt(dest, imm, true))
        return dest;
    if (genInt(dest, ~imm, false))
        return dest;

    ldr_imm(dest, imm);
    return dest;
#endif
}

// Memory load/store helpers

void ARMAssembler::dataTransfer32(DataTransferTypeA transferType, RegisterID srcDst, RegisterID base, int32_t offset)
{
    if (offset >= 0) {
        if (offset <= 0xfff)
            dtr_u(transferType, srcDst, base, offset);
        else if (offset <= 0xfffff) {
            add_r(ARMRegisters::S0, base, Op2Immediate | (offset >> 12) | (10 << 8));
            dtr_u(transferType, srcDst, ARMRegisters::S0, (offset & 0xfff));
        } else {
            moveImm(offset, ARMRegisters::S0);
            dtr_ur(transferType, srcDst, base, ARMRegisters::S0);
        }
    } else {
        if (offset >= -0xfff)
            dtr_d(transferType, srcDst, base, -offset);
        else if (offset >= -0xfffff) {
            sub_r(ARMRegisters::S0, base, Op2Immediate | (-offset >> 12) | (10 << 8));
            dtr_d(transferType, srcDst, ARMRegisters::S0, (-offset & 0xfff));
        } else {
            moveImm(offset, ARMRegisters::S0);
            dtr_ur(transferType, srcDst, base, ARMRegisters::S0);
        }
    }
}

void ARMAssembler::baseIndexTransfer32(DataTransferTypeA transferType, RegisterID srcDst, RegisterID base, RegisterID index, int scale, int32_t offset)
{
    ASSERT(scale >= 0 && scale <= 3);
    ARMWord op2 = lsl(index, scale);

    if (!offset) {
        dtr_ur(transferType, srcDst, base, op2);
        return;
    }

    add_r(ARMRegisters::S1, base, op2);
    dataTransfer32(transferType, srcDst, ARMRegisters::S1, offset);
}

void ARMAssembler::dataTransfer16(DataTransferTypeB transferType, RegisterID srcDst, RegisterID base, int32_t offset)
{
    if (offset >= 0) {
        if (offset <= 0xff)
            dtrh_u(transferType, srcDst, base, getOp2Half(offset));
        else if (offset <= 0xffff) {
            add_r(ARMRegisters::S0, base, Op2Immediate | (offset >> 8) | (12 << 8));
            dtrh_u(transferType, srcDst, ARMRegisters::S0, getOp2Half(offset & 0xff));
        } else {
            moveImm(offset, ARMRegisters::S0);
            dtrh_ur(transferType, srcDst, base, ARMRegisters::S0);
        }
    } else {
        if (offset >= -0xff)
            dtrh_d(transferType, srcDst, base, getOp2Half(-offset));
        else if (offset >= -0xffff) {
            sub_r(ARMRegisters::S0, base, Op2Immediate | (-offset >> 8) | (12 << 8));
            dtrh_d(transferType, srcDst, ARMRegisters::S0, getOp2Half(-offset & 0xff));
        } else {
            moveImm(offset, ARMRegisters::S0);
            dtrh_ur(transferType, srcDst, base, ARMRegisters::S0);
        }
    }
}

void ARMAssembler::baseIndexTransfer16(DataTransferTypeB transferType, RegisterID srcDst, RegisterID base, RegisterID index, int scale, int32_t offset)
{
    if (!scale && !offset) {
        dtrh_ur(transferType, srcDst, base, index);
        return;
    }

    add_r(ARMRegisters::S1, base, lsl(index, scale));
    dataTransfer16(transferType, srcDst, ARMRegisters::S1, offset);
}

void ARMAssembler::dataTransferFloat(DataTransferTypeFloat transferType, FPRegisterID srcDst, RegisterID base, int32_t offset)
{
    // VFP cannot directly access memory that is not four-byte-aligned
    if (!(offset & 0x3)) {
        if (offset <= 0x3ff && offset >= 0) {
            fdtr_u(transferType, srcDst, base, offset >> 2);
            return;
        }
        if (offset <= 0x3ffff && offset >= 0) {
            add_r(ARMRegisters::S0, base, Op2Immediate | (offset >> 10) | (11 << 8));
            fdtr_u(transferType, srcDst, ARMRegisters::S0, (offset >> 2) & 0xff);
            return;
        }
        offset = -offset;

        if (offset <= 0x3ff && offset >= 0) {
            fdtr_d(transferType, srcDst, base, offset >> 2);
            return;
        }
        if (offset <= 0x3ffff && offset >= 0) {
            sub_r(ARMRegisters::S0, base, Op2Immediate | (offset >> 10) | (11 << 8));
            fdtr_d(transferType, srcDst, ARMRegisters::S0, (offset >> 2) & 0xff);
            return;
        }
        offset = -offset;
    }

    moveImm(offset, ARMRegisters::S0);
    add_r(ARMRegisters::S0, ARMRegisters::S0, base);
    fdtr_u(transferType, srcDst, ARMRegisters::S0, 0);
}

void ARMAssembler::baseIndexTransferFloat(DataTransferTypeFloat transferType, FPRegisterID srcDst, RegisterID base, RegisterID index, int scale, int32_t offset)
{
    add_r(ARMRegisters::S1, base, lsl(index, scale));
    dataTransferFloat(transferType, srcDst, ARMRegisters::S1, offset);
}

PassRefPtr<ExecutableMemoryHandle> ARMAssembler::executableCopy(JSGlobalData& globalData, void* ownerUID, JITCompilationEffort effort)
{
    // 64-bit alignment is required for next constant pool and JIT code as well
    m_buffer.flushWithoutBarrier(true);
    if (!m_buffer.isAligned(8))
        bkpt(0);

    RefPtr<ExecutableMemoryHandle> result = m_buffer.executableCopy(globalData, ownerUID, effort);
    char* data = reinterpret_cast<char*>(result->start());

    for (Jumps::Iterator iter = m_jumps.begin(); iter != m_jumps.end(); ++iter) {
        // The last bit is set if the constant must be placed on constant pool.
        int pos = (iter->m_offset) & (~0x1);
        ARMWord* ldrAddr = reinterpret_cast_ptr<ARMWord*>(data + pos);
        ARMWord* addr = getLdrImmAddress(ldrAddr);
        if (*addr != InvalidBranchTarget) {
            if (!(iter->m_offset & 1)) {
                intptr_t difference = reinterpret_cast_ptr<ARMWord*>(data + *addr) - (ldrAddr + DefaultPrefetchOffset);

                if ((difference <= MaximumBranchOffsetDistance && difference >= MinimumBranchOffsetDistance)) {
                    *ldrAddr = B | getConditionalField(*ldrAddr) | (difference & BranchOffsetMask);
                    continue;
                }
            }
            *addr = reinterpret_cast<ARMWord>(data + *addr);
        }
    }

    return result;
}

#if OS(LINUX) && COMPILER(RVCT)

__asm void ARMAssembler::cacheFlush(void* code, size_t size)
{
    ARM
    push {r7}
    add r1, r1, r0
    mov r7, #0xf0000
    add r7, r7, #0x2
    mov r2, #0x0
    svc #0x0
    pop {r7}
    bx lr
}

#endif

} // namespace JSC

#endif // ENABLE(ASSEMBLER) && CPU(ARM_TRADITIONAL)
