// vstfxstore.h — VST2.x FXP/FXB preset file format
//
// Clean-room implementation. Not derived from Steinberg source.
// SPDX-License-Identifier: MIT

#pragma once
#ifndef __VSTFXSTORE__
#define __VSTFXSTORE__

#include "aeffect.h"

// File magic numbers (big-endian 4-byte tags)
#define cMagic          0x4B6E6343  // 'CcnK' — chunk header magic
#define fMagic          0x68434E46  // 'FNCh' — unused
#define bankMagic       0x6B427846  // 'FxBk' — bank file
#define presetMagic     0x6B437846  // 'FxCk' — regular preset
#define chunkPresetMagic 0x68437046 // 'FpCh' — opaque preset
#define chunkBankMagic  0x68437846  // 'FxCh' — opaque bank

#pragma pack(push, 8)

// FXP single program (parameters)
struct fxProgram
{
    VstInt32 chunkMagic;    // cMagic
    VstInt32 byteSize;      // size of this chunk, excluding chunkMagic+byteSize
    VstInt32 fxMagic;       // presetMagic or chunkPresetMagic
    VstInt32 version;       // 1
    VstInt32 fxID;          // plugin unique ID
    VstInt32 fxVersion;     // plugin version
    VstInt32 numParams;     // number of parameters
    char     prgName[28];   // program name (null-terminated)
    union {
        float  params[1];      // parameter values (numParams entries)
        struct {
            VstInt32 size;
            char     chunk[1]; // opaque chunk data
        } data;
    } content;
};

// FXB bank
struct fxBank
{
    VstInt32 chunkMagic;    // cMagic
    VstInt32 byteSize;
    VstInt32 fxMagic;       // bankMagic or chunkBankMagic
    VstInt32 version;       // 1 or 2
    VstInt32 fxID;
    VstInt32 fxVersion;
    VstInt32 numPrograms;
    VstInt32 currentProgram; // version 2+
    char     future[124];
    union {
        fxProgram programs[1]; // numPrograms entries
        struct {
            VstInt32 size;
            char     chunk[1];
        } data;
    } content;
};

#pragma pack(pop)

#endif // __VSTFXSTORE__
