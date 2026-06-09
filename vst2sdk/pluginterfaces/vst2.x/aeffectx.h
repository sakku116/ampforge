// aeffectx.h — VST2.x extended interfaces
//
// Clean-room implementation of the VST2 binary ABI (events, time info,
// speaker arrangement, parameter/pin properties, MIDI program info).
// Not derived from Steinberg source code.
//
// SPDX-License-Identifier: MIT

#pragma once
#ifndef __AEFFECTX__
#define __AEFFECTX__

#include "aeffect.h"

// ---------------------------------------------------------------------------
// AudioMaster opcodes — sent by plugin to host via audioMasterCallback
// ---------------------------------------------------------------------------
typedef enum AudioMasterOpcodes
{
    audioMasterAutomate = 0,
    audioMasterVersion,
    audioMasterCurrentId,
    audioMasterIdle,
    audioMasterPinConnected,                 // deprecated
    audioMasterWantMidi,                     // deprecated — use audioMasterUpdateDisplay
    audioMasterGetTime,
    audioMasterProcessEvents,
    audioMasterSetTime,                      // deprecated
    audioMasterTempoAt,                      // deprecated
    audioMasterGetNumAutomatableParameters,  // deprecated
    audioMasterGetParameterQuantization,     // deprecated
    audioMasterIOChanged,
    audioMasterNeedIdle,                     // deprecated
    audioMasterSizeWindow,
    audioMasterGetSampleRate,
    audioMasterGetBlockSize,
    audioMasterGetInputLatency,
    audioMasterGetOutputLatency,
    audioMasterGetPreviousPlug,              // deprecated
    audioMasterGetNextPlug,                  // deprecated
    audioMasterWillReplaceOrAccumulate,      // deprecated
    audioMasterGetCurrentProcessLevel,
    audioMasterGetAutomationState,
    audioMasterOfflineStart,
    audioMasterOfflineRead,
    audioMasterOfflineWrite,
    audioMasterOfflineGetCurrentPass,
    audioMasterOfflineGetCurrentMetaPass,
    audioMasterSetOutputSampleRate,          // deprecated
    audioMasterGetOutputSpeakerArrangement,  // deprecated
    audioMasterGetVendorString,
    audioMasterGetProductString,
    audioMasterGetVendorVersion,
    audioMasterVendorSpecific,
    audioMasterSetIcon,                      // deprecated
    audioMasterCanDo,
    audioMasterGetLanguage,
    audioMasterOpenWindow,                   // deprecated
    audioMasterCloseWindow,                  // deprecated
    audioMasterGetDirectory,
    audioMasterUpdateDisplay,
    audioMasterBeginEdit,
    audioMasterEndEdit,
    audioMasterOpenFileSelector,
    audioMasterCloseFileSelector,
    audioMasterEditFile,                     // deprecated
    audioMasterGetChunkFile,                 // deprecated
    audioMasterGetInputSpeakerArrangement    // deprecated
} AudioMasterOpcodes;

// ---------------------------------------------------------------------------
// Plugin categories (effGetPlugCategory return values)
// ---------------------------------------------------------------------------
typedef enum VstPlugCategory
{
    kPlugCategUnknown = 0,
    kPlugCategEffect,
    kPlugCategSynth,
    kPlugCategAnalysis,
    kPlugCategMastering,
    kPlugCategSpacializer,
    kPlugCategRoomFx,
    kPlugSurroundFx,
    kPlugCategRestoration,
    kPlugCategOfflineProcess,
    kPlugCategShell,
    kPlugCategGenerator,
    kPlugCategMaxCount
} VstPlugCategory;

// ---------------------------------------------------------------------------
// Process precision (effSetProcessPrecision)
// ---------------------------------------------------------------------------
typedef enum VstProcessPrecision
{
    kVstProcessPrecision32 = 0,
    kVstProcessPrecision64
} VstProcessPrecision;

// ---------------------------------------------------------------------------
// Process levels (audioMasterGetCurrentProcessLevel)
// ---------------------------------------------------------------------------
typedef enum VstProcessLevels
{
    kVstProcessLevelUnknown  = 0,
    kVstProcessLevelUser,
    kVstProcessLevelRealtime,
    kVstProcessLevelPrefetch,
    kVstProcessLevelOffline
} VstProcessLevels;

// ---------------------------------------------------------------------------
// Automation state (audioMasterGetAutomationState)
// ---------------------------------------------------------------------------
typedef enum VstAutomationStates
{
    kVstAutomationUnsupported = 0,
    kVstAutomationOff,
    kVstAutomationRead,
    kVstAutomationWrite,
    kVstAutomationReadWrite
} VstAutomationStates;

// ---------------------------------------------------------------------------
// Host language (audioMasterGetLanguage)
// ---------------------------------------------------------------------------
typedef enum VstHostLanguage
{
    kVstLangEnglish = 1,
    kVstLangGerman,
    kVstLangFrench,
    kVstLangItalian,
    kVstLangSpanish,
    kVstLangJapanese
} VstHostLanguage;

// ---------------------------------------------------------------------------
// MIDI / audio events
//
// All event structs are 32 bytes so they can be stored in a flat array.
// VstMidiSysexEvent is 48 bytes (has two pointer-sized reserved fields).
// ---------------------------------------------------------------------------
#pragma pack(push, 8)

typedef enum VstEventTypes
{
    kVstMidiType      = 1,
    kVstAudioType     = 2,    // deprecated
    kVstVideoType     = 3,    // deprecated
    kVstParameterType = 4,    // deprecated
    kVstTriggerType   = 5,    // deprecated
    kVstSysExType     = 6
} VstEventTypes;

// Base event (32 bytes)
struct VstEvent
{
    VstInt32 type;          // VstEventTypes
    VstInt32 byteSize;      // size of this event struct
    VstInt32 deltaFrames;   // sample offset from start of current block
    VstInt32 flags;         // event-specific flags
    char     data[16];      // event-type-specific data
};

// MIDI event flags
enum VstMidiEventFlags
{
    kVstMidiEventIsRealtime = 1 << 0  // event occurs on a realtime thread
};

// MIDI event (32 bytes — same size as VstEvent)
struct VstMidiEvent
{
    VstInt32 type;              // kVstMidiType
    VstInt32 byteSize;          // sizeof(VstMidiEvent)
    VstInt32 deltaFrames;       // sample offset
    VstInt32 flags;             // VstMidiEventFlags
    VstInt32 noteLength;        // note duration in sample frames (0 if n/a)
    VstInt32 noteOffset;        // note start delay in sample frames (0 if n/a)
    char     midiData[4];       // MIDI bytes 0..2, byte 3 must be 0
    char     detune;            // fine tuning -64..+63 cents
    char     noteOffVelocity;   // note-off velocity 0..127
    char     reserved1;         // 0
    char     reserved2;         // 0
};

// SysEx event (48 bytes with pack(8))
// Layout: [16 bytes header] + [4 dumpBytes] + [4 pad] + [8 resvd1] + [8 sysexDump] + [8 resvd2]
struct VstMidiSysexEvent
{
    VstInt32  type;             // kVstSysExType
    VstInt32  byteSize;         // sizeof(VstMidiSysexEvent)
    VstInt32  deltaFrames;      // sample offset
    VstInt32  flags;            // 0
    VstInt32  dumpBytes;        // byte count of sysexDump
    VstIntPtr resvd1;           // 0  (4 bytes implicit padding before this on x64)
    char*     sysexDump;        // pointer to SysEx data (host owns memory)
    VstIntPtr resvd2;           // 0
};

// Event list (variable-length; callers allocate sizeof(VstEvents) + extra event pointers)
// 32 bytes with pack(8): numEvents(4)+[4 pad]+reserved(8)+events[2](16)
struct VstEvents
{
    VstInt32  numEvents;        // number of events in list
    VstIntPtr reserved;         // 0
    VstEvent* events[2];        // variable-length array of event pointers
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Editor window rectangle (used by effEditGetRect)
// ---------------------------------------------------------------------------
#pragma pack(push, 8)

struct ERect
{
    VstInt16 top;
    VstInt16 left;
    VstInt16 bottom;
    VstInt16 right;
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Time information (returned by host on audioMasterGetTime)
// ---------------------------------------------------------------------------
#pragma pack(push, 8)

typedef enum VstTimeInfoFlags
{
    kVstTransportChanged     = 1 << 0,   // play/cycle/record state changed
    kVstTransportPlaying     = 1 << 1,
    kVstTransportCycleActive = 1 << 2,
    kVstTransportRecording   = 1 << 3,
    kVstAutomationWriting    = 1 << 6,
    kVstAutomationReading    = 1 << 7,
    kVstNanosValid           = 1 << 8,   // nanoSeconds field is valid
    kVstPpqPosValid          = 1 << 9,   // ppqPos is valid
    kVstTempoValid           = 1 << 10,  // tempo is valid
    kVstBarsValid            = 1 << 11,  // barStartPos is valid
    kVstCyclePosValid        = 1 << 12,  // cycleStart/EndPos are valid
    kVstTimeSigValid         = 1 << 13,  // timeSigNumerator/Denominator are valid
    kVstSmpteValid           = 1 << 14,  // smpteOffset/smpteFrameRate are valid
    kVstClockValid           = 1 << 15   // samplesToNextClock is valid
} VstTimeInfoFlags;

typedef enum VstSmpteFrameRate
{
    kVstSmpte24fps    =  0,
    kVstSmpte25fps    =  1,
    kVstSmpte2997fps  =  2,
    kVstSmpte30fps    =  3,
    kVstSmpte2997dfps =  4,
    kVstSmpte30dfps   =  5,
    kVstSmpteFilm16mm =  6,
    kVstSmpteFilm35mm =  7,
    kVstSmpte239fps   = 10,
    kVstSmpte249fps   = 11,
    kVstSmpte599fps   = 12,
    kVstSmpte60fps    = 13
} VstSmpteFrameRate;

struct VstTimeInfo
{
    double   samplePos;             // current playback position in samples
    double   sampleRate;            // current sample rate in Hz
    double   nanoSeconds;           // system time in nanoseconds
    double   ppqPos;                // musical position in quarter note beats
    double   tempo;                 // tempo in beats per minute
    double   barStartPos;           // last bar start position in quarter note beats
    double   cycleStartPos;         // left locator in quarter note beats
    double   cycleEndPos;           // right locator in quarter note beats
    VstInt32 timeSigNumerator;      // time signature numerator (e.g. 4 for 4/4)
    VstInt32 timeSigDenominator;    // time signature denominator (e.g. 4 for 4/4)
    VstInt32 smpteOffset;           // SMPTE offset in SMPTE subframes
    VstInt32 smpteFrameRate;        // VstSmpteFrameRate
    VstInt32 samplesToNextClock;    // MIDI clock resolution (24 per quarter note)
    VstInt32 flags;                 // VstTimeInfoFlags
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Speaker arrangement
// ---------------------------------------------------------------------------
#pragma pack(push, 8)

typedef enum VstSpeakerType
{
    kSpeakerUndefined = 0x7fffffff,
    kSpeakerM   = 0,
    kSpeakerL,
    kSpeakerR,
    kSpeakerC,
    kSpeakerLfe,
    kSpeakerLs,
    kSpeakerRs,
    kSpeakerLc,
    kSpeakerRc,
    kSpeakerS,
    kSpeakerCs = kSpeakerS,
    kSpeakerSl,
    kSpeakerSr,
    kSpeakerTm,
    kSpeakerTfl,
    kSpeakerTfc,
    kSpeakerTfr,
    kSpeakerTrl,
    kSpeakerTrc,
    kSpeakerTrr,
    kSpeakerLfe2
} VstSpeakerType;

typedef enum VstSpeakerArrangementType
{
    kSpeakerArrUserDefined = -2,
    kSpeakerArrEmpty       = -1,
    kSpeakerArrMono        =  0,
    kSpeakerArrStereo,
    kSpeakerArrStereoSurround,
    kSpeakerArrStereoCenter,
    kSpeakerArrStereoSide,
    kSpeakerArrStereoCLfe,
    kSpeakerArr30Cine,
    kSpeakerArr30Music,
    kSpeakerArr31Cine,
    kSpeakerArr31Music,
    kSpeakerArr40Cine,
    kSpeakerArr40Music,
    kSpeakerArr41Cine,
    kSpeakerArr41Music,
    kSpeakerArr50,
    kSpeakerArr51,
    kSpeakerArr60Cine,
    kSpeakerArr60Music,
    kSpeakerArr61Cine,
    kSpeakerArr61Music,
    kSpeakerArr70Cine,
    kSpeakerArr70Music,
    kSpeakerArr71Cine,
    kSpeakerArr71Music,
    kSpeakerArr80Cine,
    kSpeakerArr80Music,
    kSpeakerArr81Cine,
    kSpeakerArr81Music,
    kSpeakerArr102,
    kSpeakerArrNumArrangements
} VstSpeakerArrangementType;

#define kVstMaxSpeakerNameLen 64

// 112 bytes: azimuth(4)+elevation(4)+radius(4)+reserved(4)+name[64]+type(4)+future[28]
struct VstSpeakerProperties
{
    float    azimuth;                        // 0..360 degrees
    float    elevation;                      // -90..90 degrees
    float    radius;                         // distance 0..1
    float    reserved;                       // 0
    char     name[kVstMaxSpeakerNameLen];    // speaker name
    VstInt32 type;                           // VstSpeakerType
    char     future[28];
};

// numChannels determines how many speakers[] entries are valid
struct VstSpeakerArrangement
{
    VstInt32           type;         // VstSpeakerArrangementType
    VstInt32           numChannels;
    VstSpeakerProperties speakers[8]; // typically 2 for stereo; allocate more as needed
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Pin properties (returned by effGetInputProperties / effGetOutputProperties)
// ---------------------------------------------------------------------------
#pragma pack(push, 8)

enum VstPinPropertiesFlags
{
    kVstPinIsActive   = 1 << 0,  // pin is connected and active
    kVstPinIsStereo   = 1 << 1,  // pin is a stereo pair
    kVstPinUseSpeaker = 1 << 2   // pin has speaker arrangement info
};

struct VstPinProperties
{
    char     label[64];
    VstInt32 flags;              // VstPinPropertiesFlags
    VstInt32 arrangementType;   // VstSpeakerArrangementType
    char     shortLabel[8];
    char     future[48];
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Parameter properties (returned by effGetParameterProperties)
// ---------------------------------------------------------------------------
#pragma pack(push, 8)

enum VstParameterPropertiesFlags
{
    kVstParameterIsSwitch                = 1 << 0,
    kVstParameterUsesIntegerMinMax       = 1 << 1,
    kVstParameterUsesFloatStep           = 1 << 2,
    kVstParameterUsesIntStep             = 1 << 3,
    kVstParameterSupportsDisplayIndex    = 1 << 4,
    kVstParameterSupportsDisplayCategory = 1 << 5,
    kVstParameterCanRamp                 = 1 << 6
};

struct VstParameterProperties
{
    float    stepFloat;
    float    smallStepFloat;
    float    largeStepFloat;
    char     label[64];
    VstInt32 flags;
    VstInt32 minInteger;
    VstInt32 maxInteger;
    VstInt32 stepInteger;
    VstInt32 largeStepInteger;
    char     shortLabel[8];
    VstInt16 displayIndex;
    VstInt16 category;
    VstInt16 numParametersInCategory;
    VstInt16 reserved;
    char     categoryLabel[24];
    char     future[16];
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// MIDI program info
// ---------------------------------------------------------------------------
#pragma pack(push, 8)

struct VstMidiProgramName
{
    VstInt32 thisProgramIndex;
    char     name[64];
    char     midiProgram;        // 0..127, -1 = don't care
    char     midiBankMsb;        // 0..127, -1 = don't care
    char     midiBankLsb;        // 0..127, -1 = don't care
    char     reserved;
    VstInt32 parentCategoryIndex; // -1 = no parent
    VstInt32 flags;
};

struct VstMidiProgramCategory
{
    VstInt32 thisCategoryIndex;
    char     name[64];
    VstInt32 parentCategoryIndex; // -1 = no parent
    VstInt32 flags;
};

struct VstMidiKeyName
{
    VstInt32 thisProgramIndex;
    VstInt32 thisKeyNumber;      // 0..127
    char     keyName[64];
    VstInt32 reserved;
    VstInt32 flags;
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Patch chunk info (sent by host on effBeginLoadBank / effBeginLoadProgram)
// ---------------------------------------------------------------------------
#pragma pack(push, 8)

struct VstPatchChunkInfo
{
    VstInt32 version;           // 1
    VstInt32 pluginUniqueID;
    VstInt32 pluginVersion;
    VstInt32 numElements;       // number of programs (bank) or parameters (program)
    char     future[48];
};

#pragma pack(pop)

// ---------------------------------------------------------------------------
// Layout assertions
// ---------------------------------------------------------------------------
#ifdef __cplusplus
#include <cstddef>
static_assert(sizeof(VstEvent)          == 32,  "VstEvent size");
static_assert(sizeof(VstMidiEvent)      == 32,  "VstMidiEvent size");
static_assert(sizeof(VstMidiSysexEvent) == 48,  "VstMidiSysexEvent size");
static_assert(sizeof(VstTimeInfo)       == 88,  "VstTimeInfo size"); // 8 doubles + 6 int32s
static_assert(sizeof(VstSpeakerProperties) == 112, "VstSpeakerProperties size");
#endif

#endif // __AEFFECTX__
