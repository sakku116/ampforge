// aeffect.h — VST2.x AEffect interface
//
// Clean-room implementation of the VST2 binary ABI.
// Not derived from Steinberg source code. Based on the VST2 binary interface
// specification, cross-referenced with Xaymar/vst2sdk (BSD-3-Clause) for
// layout verification.
//
// Compatible with JUCE 8 VST2 hosting (included via juce_VSTCommon.h inside
// namespace Vst2).
//
// SPDX-License-Identifier: MIT

#pragma once
#ifndef __AEFFECT__
#define __AEFFECT__

#include <stdint.h>

// ---------------------------------------------------------------------------
// Platform calling convention
// ---------------------------------------------------------------------------
#if defined(_WIN32)
#  define VSTCALLBACK __cdecl
#else
#  define VSTCALLBACK
#endif

// ---------------------------------------------------------------------------
// Basic integer types
// ---------------------------------------------------------------------------
typedef int32_t  VstInt32;
typedef int16_t  VstInt16;
typedef intptr_t VstIntPtr;

// ---------------------------------------------------------------------------
// Plugin magic number — 'VstP' = 0x56737450
// ---------------------------------------------------------------------------
#define kEffectMagic 0x56737450

// ---------------------------------------------------------------------------
// AEffect flags (AEffect::flags field)
// ---------------------------------------------------------------------------
typedef enum AEffectFlags
{
    effFlagsHasEditor          = 1 << 0,   // plugin has its own editor
    effFlagsHasClip            = 1 << 1,   // deprecated
    effFlagsHasVu              = 1 << 2,   // deprecated
    effFlagsCanMono            = 1 << 3,   // deprecated
    effFlagsCanReplacing       = 1 << 4,   // supports processReplacing (required)
    effFlagsProgramChunks      = 1 << 5,   // program state is opaque chunk
    effFlagsSynth              = 1 << 8,   // plugin is a VSTi
    effFlagsNoSoundInStop      = 1 << 9,   // silent when not processing
    effFlagsExtIsAsync         = 1 << 10,  // deprecated
    effFlagsExtHasBuffer       = 1 << 11,  // deprecated
    effFlagsCanDoubleReplacing = 1 << 12,  // supports processDoubleReplacing
    effFlagsIsSynth            = 1 << 8    // alias for effFlagsSynth (JUCE compat)
} AEffectFlags;

// ---------------------------------------------------------------------------
// Plugin opcodes — sent by host to plugin via AEffect::dispatcher
// ---------------------------------------------------------------------------
typedef enum AEffectOpcodes
{
    effOpen = 0,
    effClose,
    effSetProgram,
    effGetProgram,
    effSetProgramName,
    effGetProgramName,
    effGetParamLabel,
    effGetParamDisplay,
    effGetParamName,
    effGetVu,                       // deprecated
    effSetSampleRate,
    effSetBlockSize,
    effMainsChanged,
    effEditGetRect,
    effEditOpen,
    effEditClose,
    effEditDraw,                    // deprecated
    effEditMouse,                   // deprecated
    effEditKey,                     // deprecated
    effEditIdle,
    effEditTop,                     // deprecated
    effEditSleep,                   // deprecated
    effIdentify,                    // deprecated
    effGetChunk,
    effSetChunk,
    effProcessEvents,
    effCanBeAutomated,
    effString2Parameter,
    effGetNumProgramCategories,     // deprecated
    effGetProgramNameIndexed,
    effCopyProgram,                 // deprecated
    effConnectInput,                // deprecated
    effConnectOutput,               // deprecated
    effGetInputProperties,
    effGetOutputProperties,
    effGetPlugCategory,
    effGetCurrentPosition,          // deprecated
    effGetDestinationBuffer,        // deprecated
    effOfflineNotify,
    effOfflinePrepare,
    effOfflineRun,
    effProcessVarIo,
    effSetSpeakerArrangement,
    effSetBlockSizeAndSampleRate,   // deprecated
    effSetBypass,
    effGetEffectName,
    effGetErrorText,                // deprecated
    effGetVendorString,
    effGetProductString,
    effGetVendorVersion,
    effVendorSpecific,
    effCanDo,
    effGetTailSize,
    effIdle,                        // deprecated
    effGetIcon,                     // deprecated
    effSetViewPosition,             // deprecated
    effGetParameterProperties,
    effKeysRequired,                // deprecated
    effGetVstVersion,
    effEditKeyDown,
    effEditKeyUp,
    effSetEditKnobMode,
    effGetMidiProgramName,
    effGetCurrentMidiProgram,
    effGetMidiProgramCategory,
    effHasMidiProgramsChanged,
    effGetMidiKeyName,
    effBeginSetProgram,
    effEndSetProgram,
    effGetSpeakerArrangement,
    effShellGetNextPlugin,
    effStartProcess,
    effStopProcess,
    effBeginLoadBank,
    effBeginLoadProgram,
    effSetProcessPrecision,
    effGetNumMidiInputChannels,
    effGetNumMidiOutputChannels
} AEffectOpcodes;

// ---------------------------------------------------------------------------
// String length constants
// ---------------------------------------------------------------------------
enum VstStringConstants
{
    kVstMaxProgNameLen   = 24,
    kVstMaxParamStrLen   = 8,
    kVstMaxVendorStrLen  = 64,
    kVstMaxProductStrLen = 64,
    kVstMaxEffectNameLen = 32
};

// ---------------------------------------------------------------------------
// AEffect forward declaration
// ---------------------------------------------------------------------------
struct AEffect;

// ---------------------------------------------------------------------------
// Function pointer typedefs
// ---------------------------------------------------------------------------

// Host callback (plugin → host)
typedef VstIntPtr (VSTCALLBACK* audioMasterCallback)(
    struct AEffect* effect,
    VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);

// Dispatcher (host → plugin)
typedef VstIntPtr (VSTCALLBACK* AEffectDispatcherProc)(
    struct AEffect* effect,
    VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt);

// Audio processing (deprecated, accumulating)
typedef void (VSTCALLBACK* AEffectProcessProc)(
    struct AEffect* effect,
    float** inputs, float** outputs, VstInt32 sampleFrames);

// Audio processing (32-bit replacing, required)
typedef void (VSTCALLBACK* AEffectProcessReplProc)(
    struct AEffect* effect,
    float** inputs, float** outputs, VstInt32 sampleFrames);

// Audio processing (64-bit replacing)
typedef void (VSTCALLBACK* AEffectProcessDoubleProc)(
    struct AEffect* effect,
    double** inputs, double** outputs, VstInt32 sampleFrames);

// Parameter access
typedef void  (VSTCALLBACK* AEffectSetParameterProc)(struct AEffect* effect, VstInt32 index, float value);
typedef float (VSTCALLBACK* AEffectGetParameterProc)(struct AEffect* effect, VstInt32 index);

// ---------------------------------------------------------------------------
// AEffect — main plugin struct
//
// Binary layout (x64, #pragma pack(8)):
//   offset   0 : magic          (int32,  4 bytes)
//   offset   8 : dispatcher     (ptr,    8 bytes) — 4 bytes implicit padding before
//   offset  16 : process        (ptr,    8 bytes)
//   offset  24 : setParameter   (ptr,    8 bytes)
//   offset  32 : getParameter   (ptr,    8 bytes)
//   offset  40 : numPrograms    (int32,  4 bytes)
//   offset  44 : numParams      (int32,  4 bytes)
//   offset  48 : numInputs      (int32,  4 bytes)
//   offset  52 : numOutputs     (int32,  4 bytes)
//   offset  56 : flags          (int32,  4 bytes)
//   offset  64 : resvd1         (intptr, 8 bytes) — 4 bytes implicit padding before
//   offset  72 : resvd2         (intptr, 8 bytes)
//   offset  80 : initialDelay   (int32,  4 bytes)
//   offset  84 : realQualities  (int32,  4 bytes)
//   offset  88 : offQualities   (int32,  4 bytes)
//   offset  92 : ioRatio        (float,  4 bytes)
//   offset  96 : object         (ptr,    8 bytes)
//   offset 104 : user           (ptr,    8 bytes)
//   offset 112 : uniqueID       (int32,  4 bytes)
//   offset 116 : version        (int32,  4 bytes)
//   offset 120 : processReplacing        (ptr, 8 bytes)
//   offset 128 : processDoubleReplacing  (ptr, 8 bytes)
//   offset 136 : future[56]     (char[], 56 bytes)
//   total: 192 bytes
// ---------------------------------------------------------------------------
#pragma pack(push, 8)
struct AEffect
{
    VstInt32 magic;                              // kEffectMagic

    AEffectDispatcherProc   dispatcher;          // host sends commands to plugin
    AEffectProcessProc      process;             // deprecated; use processReplacing
    AEffectSetParameterProc setParameter;
    AEffectGetParameterProc getParameter;

    VstInt32 numPrograms;
    VstInt32 numParams;
    VstInt32 numInputs;
    VstInt32 numOutputs;
    VstInt32 flags;                              // AEffectFlags

    VstIntPtr resvd1;                            // reserved, host sets to 0
    VstIntPtr resvd2;

    VstInt32 initialDelay;                       // latency in samples
    VstInt32 realQualities;                      // deprecated
    VstInt32 offQualities;                       // deprecated
    float    ioRatio;                            // deprecated

    void* object;                                // for plugin's internal use
    void* user;                                  // for host's internal use

    VstInt32 uniqueID;
    VstInt32 version;

    AEffectProcessReplProc   processReplacing;        // 32-bit replacing
    AEffectProcessDoubleProc processDoubleReplacing;  // 64-bit replacing

    char future[56];
};
#pragma pack(pop)

// Entry point exported by plugin DLL (name: VSTPluginMain or main)
typedef AEffect* (VSTCALLBACK* PluginEntryProc)(audioMasterCallback hostCallback);

// ---------------------------------------------------------------------------
// Layout assertions (C++ only — JUCE compiles these headers as C++)
// ---------------------------------------------------------------------------
#ifdef __cplusplus
#include <cstddef>
static_assert(sizeof(AEffect) == 192,                            "AEffect size mismatch");
static_assert(offsetof(AEffect, dispatcher)   == 8,             "AEffect::dispatcher offset");
static_assert(offsetof(AEffect, numInputs)    == 48,            "AEffect::numInputs offset");
static_assert(offsetof(AEffect, flags)        == 56,            "AEffect::flags offset");
static_assert(offsetof(AEffect, resvd1)       == 64,            "AEffect::resvd1 offset");
static_assert(offsetof(AEffect, initialDelay) == 80,            "AEffect::initialDelay offset");
static_assert(offsetof(AEffect, object)       == 96,            "AEffect::object offset");
static_assert(offsetof(AEffect, uniqueID)     == 112,           "AEffect::uniqueID offset");
static_assert(offsetof(AEffect, processReplacing) == 120,       "AEffect::processReplacing offset");
static_assert(offsetof(AEffect, future)       == 136,           "AEffect::future offset");
#endif

#endif // __AEFFECT__
