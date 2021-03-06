/**********************************************************************

  Audacity: A Digital Audio Editor

  Distortion.cpp

  Steve Daulton

  // TODO: Add a graph display of the waveshaper equation.
  // TODO: Allow the user to draw the graph.

******************************************************************//**

\class EffectDistortion
\brief A WaveShaper distortion effect.

*//*******************************************************************/

#include "../Audacity.h"
#include "Distortion.h"

#include <cmath>
#include <algorithm>
#define _USE_MATH_DEFINES

// Belt and braces
#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif
#ifndef M_PI_2
#define M_PI_2 1.57079632679489661923132169163975 
#endif

#include <wx/choice.h>
#include <wx/intl.h>
#include <wx/valgen.h>
#include <wx/log.h>

#include "../Prefs.h"
#include "../ShuttleGui.h"
#include "../widgets/valnum.h"

enum kTableType
{
   kHardClip,
   kSoftClip,
   kHalfSinCurve,
   kExpCurve,
   kLogCurve,
   kCubic,
   kEvenHarmonics,
   kSinCurve,
   kLeveller,
   kRectifier,
   kHardLimiter,
   kNumTableTypes
};

static const wxString kTableTypeStrings[kNumTableTypes] =
{
   XO("Hard Clipping"),
   XO("Soft Clipping"),
   XO("Soft Overdrive"),
   XO("Medium Overdrive"),
   XO("Hard Overdrive"),
   XO("Cubic Curve (odd harmonics)"),
   XO("Even Harmonics"),
   XO("Expand and Compress"),
   XO("Leveller"),
   XO("Rectifier Distortion"),
   XO("Hard Limiter 1413")
};

// Define keys, defaults, minimums, and maximums for the effect parameters
// (Note: 'Repeats' is the total number of times the effect is applied.)
//
//     Name             Type     Key                   Def     Min      Max                 Scale
Param( TableTypeIndx,   int,     XO("Type"),           0,       0,      kNumTableTypes-1,    1    );
Param( DCBlock,         bool,    XO("DC Block"),      false,   false,   true,                1    );
Param( Threshold_dB,    double,  XO("Threshold dB"),  -6.0,  -100.0,     0.0,             1000.0f );
Param( NoiseFloor,      double,  XO("Noise Floor"),   -70.0,  -80.0,   -20.0,                1    );
Param( Param1,          double,  XO("Parameter 1"),    50.0,    0.0,   100.0,                1    );
Param( Param2,          double,  XO("Parameter 2"),    50.0,    0.0,   100.0,                1    );
Param( Repeats,         int,     XO("Repeats"),        1,       0,       5,                  1    );

// How many samples are processed before recomputing the lookup table again
#define skipsamples 1000

const double MIN_Threshold_Linear DB_TO_LINEAR(MIN_Threshold_dB);

static const struct
{
   const wxChar *name;
   EffectDistortion::Params params;
}
FactoryPresets[] =
{
   //                                           Table    DCBlock  threshold   floor       Param1   Param2   Repeats
   // Defaults:                                   0       false   -6.0       -70.0(off)     50.0     50.0     1
   //
   XO("Hard clip -12dB, 80% make-up gain"),     { 0,        0,      -12.0,      -70.0,      0.0,     80.0,    0 },
   XO("Soft clip -12dB, 80% make-up gain"),     { 1,        0,      -12.0,      -70.0,      50.0,    80.0,    0 },
   XO("Fuzz Box"),                              { 1,        0,      -30.0,      -70.0,      80.0,    80.0,    0 },
   XO("Walkie-talkie"),                         { 1,        0,      -50.0,      -70.0,      60.0,    80.0,    0 },
   XO("Blues drive sustain"),                   { 2,        0,       -6.0,      -70.0,      30.0,    80.0,    0 },
   XO("Light Crunch Overdrive"),                { 3,        0,       -6.0,      -70.0,      20.0,    80.0,    0 },
   XO("Heavy Overdrive"),                       { 4,        0,       -6.0,      -70.0,      90.0,    80.0,    0 },
   XO("3rd Harmonic (Perfect Fifth)"),          { 5,        0,       -6.0,      -70.0,     100.0,    60.0,    0 },
   XO("Valve Overdrive"),                       { 6,        1,       -6.0,      -70.0,      30.0,    40.0,    0 },
   XO("2nd Harmonic (Octave)"),                 { 6,        1,       -6.0,      -70.0,      50.0,     0.0,    0 },
   XO("Gated Expansion Distortion"),            { 7,        0,       -6.0,      -70.0,      30.0,    80.0,    0 },
   XO("Leveller, Light, -70dB noise floor"),    { 8,        0,       -6.0,      -70.0,       0.0,    50.0,    1 },
   XO("Leveller, Moderate, -70dB noise floor"), { 8,        0,       -6.0,      -70.0,       0.0,    50.0,    2 },
   XO("Leveller, Heavy, -70dB noise floor"),    { 8,        0,       -6.0,      -70.0,       0.0,    50.0,    3 },
   XO("Leveller, Heavier, -70dB noise floor"),  { 8,        0,       -6.0,      -70.0,       0.0,    50.0,    4 },
   XO("Leveller, Heaviest, -70dB noise floor"), { 8,        0,       -6.0,      -70.0,       0.0,    50.0,    5 },
   XO("Half-wave Rectifier"),                   { 9,        0,       -6.0,      -70.0,      50.0,    50.0,    0 },
   XO("Full-wave Rectifier"),                   { 9,        0,       -6.0,      -70.0,     100.0,    50.0,    0 },
   XO("Full-wave Rectifier (DC blocked)"),      { 9,        1,       -6.0,      -70.0,     100.0,    50.0,    0 },
   XO("Percussion Limiter"),                    {10,        0,      -12.0,      -70.0,     100.0,    30.0,    0 },
};

const wxString defaultLabel[5] =
{
   _("Upper Threshold"),
   _("Noise Floor"),
   _("Parameter 1"),
   _("Parameter 2"),
   _("Number of repeats"),
};

#include <wx/arrimpl.cpp>
WX_DEFINE_OBJARRAY(EffectDistortionStateArray);
 
//
// EffectDistortion
//

BEGIN_EVENT_TABLE(EffectDistortion, wxEvtHandler)
   EVT_CHOICE(ID_Type, EffectDistortion::OnTypeChoice)
   EVT_CHECKBOX(ID_DCBlock, EffectDistortion::OnDCBlockCheckbox)
   EVT_TEXT(ID_Threshold, EffectDistortion::OnThresholdText)
   EVT_SLIDER(ID_Threshold, EffectDistortion::OnThresholdSlider)
   EVT_TEXT(ID_NoiseFloor, EffectDistortion::OnNoiseFloorText)
   EVT_SLIDER(ID_NoiseFloor, EffectDistortion::OnNoiseFloorSlider)
   EVT_TEXT(ID_Param1, EffectDistortion::OnParam1Text)
   EVT_SLIDER(ID_Param1, EffectDistortion::OnParam1Slider)
   EVT_TEXT(ID_Param2, EffectDistortion::OnParam2Text)
   EVT_SLIDER(ID_Param2, EffectDistortion::OnParam2Slider)
   EVT_TEXT(ID_Repeats, EffectDistortion::OnRepeatsText)
   EVT_SLIDER(ID_Repeats, EffectDistortion::OnRepeatsSlider)
END_EVENT_TABLE()

EffectDistortion::EffectDistortion()
{
   wxASSERT(kNumTableTypes == WXSIZEOF(kTableTypeStrings));

   mParams.mTableChoiceIndx = DEF_TableTypeIndx;
   mParams.mDCBlock = DEF_DCBlock;
   mParams.mThreshold_dB = DEF_Threshold_dB;
   mThreshold = DB_TO_LINEAR(mParams.mThreshold_dB);
   mParams.mNoiseFloor = DEF_NoiseFloor;
   mParams.mParam1 = DEF_Param1;
   mParams.mParam2 = DEF_Param2;
   mParams.mRepeats = DEF_Repeats;
   mMakeupGain = 1.0;
   mbSavedFilterState = DEF_DCBlock;

   for (int i = 0; i < kNumTableTypes; i++)
   {
      mTableTypes.Add(wxGetTranslation(kTableTypeStrings[i]));
   }

   SetLinearEffectFlag(false);
}

EffectDistortion::~EffectDistortion()
{
}

// IdentInterface implementation

wxString EffectDistortion::GetSymbol()
{
   return DISTORTION_PLUGIN_SYMBOL;
}

wxString EffectDistortion::GetDescription()
{
   return XO("Waveshaping distortion effect");
}

// EffectIdentInterface implementation

EffectType EffectDistortion::GetType()
{
   return EffectTypeProcess;
}

bool EffectDistortion::SupportsRealtime()
{
#if defined(EXPERIMENTAL_REALTIME_AUDACITY_EFFECTS)
   return true;
#else
   return false;
#endif
}

// EffectClientInterface implementation

int EffectDistortion::GetAudioInCount()
{
   return 1;
}

int EffectDistortion::GetAudioOutCount()
{
   return 1;
}

bool EffectDistortion::ProcessInitialize(sampleCount WXUNUSED(totalLen), ChannelNames WXUNUSED(chanMap))
{
   InstanceInit(mMaster, mSampleRate);
   return true;
}

sampleCount EffectDistortion::ProcessBlock(float **inBlock, float **outBlock, sampleCount blockLen)
{
   return InstanceProcess(mMaster, inBlock, outBlock, blockLen);
}

bool EffectDistortion::RealtimeInitialize()
{
   SetBlockSize(512);

   mSlaves.Clear();

   return true;
}

bool EffectDistortion::RealtimeAddProcessor(int WXUNUSED(numChannels), float sampleRate)
{
   EffectDistortionState slave;

   InstanceInit(slave, sampleRate);

   mSlaves.Add(slave);

   return true;
}

bool EffectDistortion::RealtimeFinalize()
{
   mSlaves.Clear();

   return true;
}

sampleCount EffectDistortion::RealtimeProcess(int group,
                                              float **inbuf,
                                              float **outbuf,
                                              sampleCount numSamples)
{

   return InstanceProcess(mSlaves[group], inbuf, outbuf, numSamples);
}

bool EffectDistortion::GetAutomationParameters(EffectAutomationParameters & parms)
{
   parms.Write(KEY_TableTypeIndx, kTableTypeStrings[mParams.mTableChoiceIndx]);
   parms.Write(KEY_DCBlock, mParams.mDCBlock);
   parms.Write(KEY_Threshold_dB, mParams.mThreshold_dB);
   parms.Write(KEY_NoiseFloor, mParams.mNoiseFloor);
   parms.Write(KEY_Param1, mParams.mParam1);
   parms.Write(KEY_Param2, mParams.mParam2);
   parms.Write(KEY_Repeats, mParams.mRepeats);

   return true;
}

bool EffectDistortion::SetAutomationParameters(EffectAutomationParameters & parms)
{
   ReadAndVerifyEnum(TableTypeIndx,  wxArrayString(kNumTableTypes, kTableTypeStrings));
   ReadAndVerifyBool(DCBlock);
   ReadAndVerifyDouble(Threshold_dB);
   ReadAndVerifyDouble(NoiseFloor);
   ReadAndVerifyDouble(Param1);
   ReadAndVerifyDouble(Param2);
   ReadAndVerifyInt(Repeats);

   mParams.mTableChoiceIndx = TableTypeIndx;
   mParams.mDCBlock = DCBlock;
   mParams.mThreshold_dB = Threshold_dB;
   mParams.mNoiseFloor = NoiseFloor;
   mParams.mParam1 = Param1;
   mParams.mParam2 = Param2;
   mParams.mRepeats = Repeats;

   return true;
}

wxArrayString EffectDistortion::GetFactoryPresets()
{
   wxArrayString names;

   for (size_t i = 0; i < WXSIZEOF(FactoryPresets); i++)
   {
      names.Add(wxGetTranslation(FactoryPresets[i].name));
   }

   return names;
}

bool EffectDistortion::LoadFactoryPreset(int id)
{
   if (id < 0 || id >= (int) WXSIZEOF(FactoryPresets))
   {
      return false;
   }

   mParams = FactoryPresets[id].params;
   mThreshold = DB_TO_LINEAR(mParams.mThreshold_dB);

   if (mUIDialog)
   {
      TransferDataToWindow();
   }

   return true;
}


// Effect implementation

void EffectDistortion::PopulateOrExchange(ShuttleGui & S)
{
   S.AddSpace(0, 5);
   S.StartVerticalLay();
   {
      S.StartMultiColumn(4, wxCENTER);
      {
         mTypeChoiceCtrl = S.Id(ID_Type).AddChoice(_("Distortion type:"), wxT(""), &mTableTypes);
         mTypeChoiceCtrl->SetValidator(wxGenericValidator(&mParams.mTableChoiceIndx));
         S.SetSizeHints(-1, -1);

         mDCBlockCheckBox = S.Id(ID_DCBlock).AddCheckBox(_("DC blocking filter"),
                                       DEF_DCBlock ? wxT("true") : wxT("false"));
      }
      S.EndMultiColumn();
      S.AddSpace(0, 10);


      S.StartStatic(_("Threshold controls"));
      {
         S.StartMultiColumn(4, wxEXPAND);
         S.SetStretchyCol(2);
         {
            // Allow space for first Column
            S.AddSpace(250,0); S.AddSpace(0,0); S.AddSpace(0,0); S.AddSpace(0,0); 

            // Upper threshold control
            mThresholdTxt = S.AddVariableText(defaultLabel[0], false, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT);
            FloatingPointValidator<double> vldThreshold(2, &mParams.mThreshold_dB);
            vldThreshold.SetRange(MIN_Threshold_dB, MAX_Threshold_dB);
            mThresholdT = S.Id(ID_Threshold).AddTextBox(wxT(""), wxT(""), 10);
            mThresholdT->SetName(defaultLabel[0]);
            mThresholdT->SetValidator(vldThreshold);

            S.SetStyle(wxSL_HORIZONTAL);
            double maxLin = DB_TO_LINEAR(MAX_Threshold_dB) * SCL_Threshold_dB;
            double minLin = DB_TO_LINEAR(MIN_Threshold_dB) * SCL_Threshold_dB;
            mThresholdS = S.Id(ID_Threshold).AddSlider(wxT(""), 0, maxLin, minLin);
            mThresholdS->SetName(defaultLabel[0]);
            S.AddSpace(20, 0);

            // Noise floor control
            mNoiseFloorTxt = S.AddVariableText(defaultLabel[1], false, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT);
            FloatingPointValidator<double> vldfloor(2, &mParams.mNoiseFloor);
            vldfloor.SetRange(MIN_NoiseFloor, MAX_NoiseFloor);
            mNoiseFloorT = S.Id(ID_NoiseFloor).AddTextBox(wxT(""), wxT(""), 10);
            mNoiseFloorT->SetName(defaultLabel[1]);
            mNoiseFloorT->SetValidator(vldfloor);

            S.SetStyle(wxSL_HORIZONTAL);
            mNoiseFloorS = S.Id(ID_NoiseFloor).AddSlider(wxT(""), 0, MAX_NoiseFloor, MIN_NoiseFloor);
            mNoiseFloorS->SetName(defaultLabel[1]);
            S.AddSpace(20, 0);
         }
         S.EndMultiColumn();
      }
      S.EndStatic();

      S.StartStatic(_("Parameter controls"));
      {
         S.StartMultiColumn(4, wxEXPAND);
         S.SetStretchyCol(2);
         {
            // Allow space for first Column
            S.AddSpace(250,0); S.AddSpace(0,0); S.AddSpace(0,0); S.AddSpace(0,0); 

            // Parameter1 control
            mParam1Txt = S.AddVariableText(defaultLabel[2], false, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT);
            FloatingPointValidator<double> vldparam1(2, &mParams.mParam1);
            vldparam1.SetRange(MIN_Param1, MAX_Param1);
            mParam1T = S.Id(ID_Param1).AddTextBox(wxT(""), wxT(""), 10);
            mParam1T->SetName(defaultLabel[2]);
            mParam1T->SetValidator(vldparam1);

            S.SetStyle(wxSL_HORIZONTAL);
            mParam1S = S.Id(ID_Param1).AddSlider(wxT(""), 0, MAX_Param1, MIN_Param1);
            mParam1S->SetName(defaultLabel[2]);
            S.AddSpace(20, 0);

            // Parameter2 control
            mParam2Txt = S.AddVariableText(defaultLabel[3], false, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT);
            FloatingPointValidator<double> vldParam2(2, &mParams.mParam2);
            vldParam2.SetRange(MIN_Param2, MAX_Param2);
            mParam2T = S.Id(ID_Param2).AddTextBox(wxT(""), wxT(""), 10);
            mParam2T->SetName(defaultLabel[3]);
            mParam2T->SetValidator(vldParam2);

            S.SetStyle(wxSL_HORIZONTAL);
            mParam2S = S.Id(ID_Param2).AddSlider(wxT(""), 0, MAX_Param2, MIN_Param2);
            mParam2S->SetName(defaultLabel[3]);
            S.AddSpace(20, 0);

            // Repeats control
            mRepeatsTxt = S.AddVariableText(defaultLabel[4], false, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT);
            IntegerValidator<int>vldRepeats(&mParams.mRepeats);
            vldRepeats.SetRange(MIN_Repeats, MAX_Repeats);
            mRepeatsT = S.Id(ID_Repeats).AddTextBox(wxT(""), wxT(""), 10);
            mRepeatsT->SetName(defaultLabel[4]);
            mRepeatsT->SetValidator(vldRepeats);

            S.SetStyle(wxSL_HORIZONTAL);
            mRepeatsS = S.Id(ID_Repeats).AddSlider(wxT(""), DEF_Repeats, MAX_Repeats, MIN_Repeats);
            mRepeatsS->SetName(defaultLabel[4]);
            S.AddSpace(20, 0);
         }
         S.EndMultiColumn();
      }
      S.EndStatic();
   }
   S.EndVerticalLay();

   return;
}

bool EffectDistortion::TransferDataToWindow()
{
   if (!mUIParent->TransferDataToWindow())
   {
      return false;
   }

   mThresholdS->SetValue((int) (mThreshold * SCL_Threshold_dB + 0.5));
   mDCBlockCheckBox->SetValue(mParams.mDCBlock);
   mNoiseFloorS->SetValue((int) mParams.mNoiseFloor + 0.5);
   mParam1S->SetValue((int) mParams.mParam1 + 0.5);
   mParam2S->SetValue((int) mParams.mParam2 + 0.5);
   mRepeatsS->SetValue(mParams.mRepeats);

   mbSavedFilterState = mParams.mDCBlock;

   UpdateUI();

   return true;
}

bool EffectDistortion::TransferDataFromWindow()
{
   if (!mUIParent->Validate() || !mUIParent->TransferDataFromWindow())
   {
      return false;
   }

   mThreshold = DB_TO_LINEAR(mParams.mThreshold_dB);

   return true;
}

void EffectDistortion::InstanceInit(EffectDistortionState & data, float sampleRate)
{
   data.samplerate = sampleRate;
   data.skipcount = 0;
   data.tablechoiceindx = mParams.mTableChoiceIndx;
   data.dcblock = mParams.mDCBlock;
   data.threshold = mParams.mThreshold_dB;
   data.noisefloor = mParams.mNoiseFloor;
   data.param1 = mParams.mParam1;
   data.param2 = mParams.mParam2;
   data.repeats = mParams.mRepeats;

   // DC block filter variables
   data.queuetotal = 0.0;

   //std::queue<float>().swap(data.queuesamples);
   while (!data.queuesamples.empty())
      data.queuesamples.pop();

   MakeTable();

   return;
}

sampleCount EffectDistortion::InstanceProcess(EffectDistortionState& data, float** inBlock, float** outBlock, sampleCount blockLen)
{
   float *ibuf = inBlock[0];
   float *obuf = outBlock[0];

   bool update = (mParams.mTableChoiceIndx == data.tablechoiceindx &&
                  mParams.mNoiseFloor == data.noisefloor &&
                  mParams.mThreshold_dB == data.threshold &&
                  mParams.mParam1 == data.param1 &&
                  mParams.mParam2 == data.param2 &&
                  mParams.mRepeats == data.repeats)? false : true;

   double p1 = mParams.mParam1 / 100.0;
   double p2 = mParams.mParam2 / 100.0;

   data.tablechoiceindx = mParams.mTableChoiceIndx;
   data.threshold = mParams.mThreshold_dB;
   data.noisefloor = mParams.mNoiseFloor;
   data.param1 = mParams.mParam1;
   data.repeats = mParams.mRepeats;

   for (sampleCount i = 0; i < blockLen; i++) {
      if (update && ((data.skipcount++) % skipsamples == 0)) {
         MakeTable();
      }

      switch (mParams.mTableChoiceIndx)
      {
      case kHardClip:
         // Param2 = make-up gain.
         obuf[i] = WaveShaper(ibuf[i]) * ((1 - p2) + (mMakeupGain * p2));
         break;
      case kSoftClip:
         // Param2 = make-up gain.
         obuf[i] = WaveShaper(ibuf[i]) * ((1 - p2) + (mMakeupGain * p2));
         break;
      case kHalfSinCurve:
         obuf[i] = WaveShaper(ibuf[i]) * p2;
         break;
      case kExpCurve:
         obuf[i] = WaveShaper(ibuf[i]) * p2;
         break;
      case kLogCurve:
         obuf[i] = WaveShaper(ibuf[i]) * p2;
         break;
      case kCubic:
         obuf[i] = WaveShaper(ibuf[i]) * p2;
         break;
      case kEvenHarmonics:
         obuf[i] = WaveShaper(ibuf[i]);
         break;
      case kSinCurve:
         obuf[i] = WaveShaper(ibuf[i]) * p2;
         break;
      case kLeveller:
         obuf[i] = WaveShaper(ibuf[i]);
         break;
      case kRectifier:
         obuf[i] = WaveShaper(ibuf[i]);
         break;
      case kHardLimiter:
         // Mix equivalent to LADSPA effect's "Wet / Residual" mix
         obuf[i] = (WaveShaper(ibuf[i]) * (p1 - p2)) + (ibuf[i] * p2);
         break;
      default:
         obuf[i] = WaveShaper(ibuf[i]);
      }
      if (mParams.mDCBlock) {
         obuf[i] = DCFilter(data, obuf[i]);
      }
   }

   return blockLen;
}

void EffectDistortion::OnTypeChoice(wxCommandEvent& evt)
{
   mTypeChoiceCtrl->GetValidator()->TransferFromWindow();

   UpdateUI();
}

void EffectDistortion::OnDCBlockCheckbox(wxCommandEvent& evt)
{
   mParams.mDCBlock = mDCBlockCheckBox->GetValue();
   mbSavedFilterState = mParams.mDCBlock;
}


void EffectDistortion::OnThresholdText(wxCommandEvent& evt)
{
   mThresholdT->GetValidator()->TransferFromWindow();
   mThreshold = DB_TO_LINEAR(mParams.mThreshold_dB);
   mThresholdS->SetValue((int) (mThreshold * SCL_Threshold_dB + 0.5));
}

void EffectDistortion::OnThresholdSlider(wxCommandEvent& evt)
{
   mThreshold = (double) evt.GetInt() / SCL_Threshold_dB;
   mParams.mThreshold_dB = wxMax(LINEAR_TO_DB(mThreshold), MIN_Threshold_dB);
   mThreshold = std::max(MIN_Threshold_Linear, mThreshold);
   mThresholdT->GetValidator()->TransferToWindow();
}

void EffectDistortion::OnNoiseFloorText(wxCommandEvent& evt)
{
   mNoiseFloorT->GetValidator()->TransferFromWindow();
   mNoiseFloorS->SetValue((int) floor(mParams.mNoiseFloor + 0.5));
}

void EffectDistortion::OnNoiseFloorSlider(wxCommandEvent& evt)
{
   mParams.mNoiseFloor = (double) evt.GetInt();
   mNoiseFloorT->GetValidator()->TransferToWindow();
}


void EffectDistortion::OnParam1Text(wxCommandEvent& evt)
{
   mParam1T->GetValidator()->TransferFromWindow();
   mParam1S->SetValue((int) floor(mParams.mParam1 + 0.5));
}

void EffectDistortion::OnParam1Slider(wxCommandEvent& evt)
{
   mParams.mParam1 = (double) evt.GetInt();
   mParam1T->GetValidator()->TransferToWindow();
}

void EffectDistortion::OnParam2Text(wxCommandEvent& evt)
{
   mParam2T->GetValidator()->TransferFromWindow();
   mParam2S->SetValue((int) floor(mParams.mParam2 + 0.5));
}

void EffectDistortion::OnParam2Slider(wxCommandEvent& evt)
{
   mParams.mParam2 = (double) evt.GetInt();
   mParam2T->GetValidator()->TransferToWindow();
}

void EffectDistortion::OnRepeatsText(wxCommandEvent& evt)
{
   mRepeatsT->GetValidator()->TransferFromWindow();
   mRepeatsS->SetValue(mParams.mRepeats);
}

void EffectDistortion::OnRepeatsSlider(wxCommandEvent& evt)
{
   mParams.mRepeats = evt.GetInt();
   mRepeatsT->GetValidator()->TransferToWindow();
   
}

void EffectDistortion::UpdateUI()
{
   // set control text and names to match distortion type
   switch (mParams.mTableChoiceIndx)
      {
      case kHardClip:
         UpdateControlText(mThresholdT, mOldThresholdTxt, true);
         UpdateControlText(mNoiseFloorT, mOldmNoiseFloorTxt, false);
         UpdateControlText(mParam1T, mOldParam1Txt, true);
         UpdateControlText(mParam2T, mOldParam2Txt, true);
         UpdateControlText(mRepeatsT, mOldRepeatsTxt, false);

         UpdateControl(ID_Threshold, true, _("Clipping level"));
         UpdateControl(ID_NoiseFloor, false, defaultLabel[1]);
         UpdateControl(ID_Param1, true, _("Drive"));
         UpdateControl(ID_Param2, true, _("Make-up Gain"));
         UpdateControl(ID_Repeats, false, defaultLabel[4]);
         UpdateControl(ID_DCBlock, false, wxEmptyString);
         break;
      case kSoftClip:
         UpdateControlText(mThresholdT, mOldThresholdTxt, true);
         UpdateControlText(mNoiseFloorT, mOldmNoiseFloorTxt, false);
         UpdateControlText(mParam1T, mOldParam1Txt, true);
         UpdateControlText(mParam2T, mOldParam2Txt, true);
         UpdateControlText(mRepeatsT, mOldRepeatsTxt, false);

         UpdateControl(ID_Threshold, true, _("Clipping threshold"));
         UpdateControl(ID_NoiseFloor, false, defaultLabel[1]);
         UpdateControl(ID_Param1, true, _("Hardness"));
         UpdateControl(ID_Param2, true, _("Make-up Gain"));
         UpdateControl(ID_Repeats, false, defaultLabel[4]);
         UpdateControl(ID_DCBlock, false, wxEmptyString);
         break;
      case kHalfSinCurve:
         UpdateControlText(mThresholdT, mOldThresholdTxt, false);
         UpdateControlText(mNoiseFloorT, mOldmNoiseFloorTxt, false);
         UpdateControlText(mParam1T, mOldParam1Txt, true);
         UpdateControlText(mParam2T, mOldParam2Txt, true);
         UpdateControlText(mRepeatsT, mOldRepeatsTxt, false);

         UpdateControl(ID_Threshold, false, defaultLabel[0]);
         UpdateControl(ID_NoiseFloor, false, defaultLabel[1]);
         UpdateControl(ID_Param1, true, _("Distortion amount"));
         UpdateControl(ID_Param2, true, _("Output level"));
         UpdateControl(ID_Repeats, false, defaultLabel[4]);
         UpdateControl(ID_DCBlock, false, wxEmptyString);
         break;
      case kExpCurve:
         UpdateControlText(mThresholdT, mOldThresholdTxt, false);
         UpdateControlText(mNoiseFloorT, mOldmNoiseFloorTxt, false);
         UpdateControlText(mParam1T, mOldParam1Txt, true);
         UpdateControlText(mParam2T, mOldParam2Txt, true);
         UpdateControlText(mRepeatsT, mOldRepeatsTxt, false);

         UpdateControl(ID_Threshold, false, defaultLabel[0]);
         UpdateControl(ID_NoiseFloor, false, defaultLabel[1]);
         UpdateControl(ID_Param1, true, _("Distortion amount"));
         UpdateControl(ID_Param2, true, _("Output level"));
         UpdateControl(ID_Repeats, false, defaultLabel[4]);
         UpdateControl(ID_DCBlock, false, wxEmptyString);
         break;
      case kLogCurve:
         UpdateControlText(mThresholdT, mOldThresholdTxt, false);
         UpdateControlText(mNoiseFloorT, mOldmNoiseFloorTxt, false);
         UpdateControlText(mParam1T, mOldParam1Txt, true);
         UpdateControlText(mParam2T, mOldParam2Txt, true);
         UpdateControlText(mRepeatsT, mOldRepeatsTxt, false);

         UpdateControl(ID_Threshold, false, defaultLabel[0]);
         UpdateControl(ID_NoiseFloor, false, defaultLabel[1]);
         UpdateControl(ID_Param1, true, _("Distortion amount"));
         UpdateControl(ID_Param2, true, _("Output level"));
         UpdateControl(ID_Repeats, false, defaultLabel[4]);
         UpdateControl(ID_DCBlock, false, wxEmptyString);
         break;
      case kCubic:
         UpdateControlText(mThresholdT, mOldThresholdTxt, false);
         UpdateControlText(mNoiseFloorT, mOldmNoiseFloorTxt, false);
         UpdateControlText(mParam1T, mOldParam1Txt, true);
         UpdateControlText(mParam2T, mOldParam2Txt, true);
         UpdateControlText(mRepeatsT, mOldRepeatsTxt, true);

         UpdateControl(ID_Threshold, false, defaultLabel[0]);
         UpdateControl(ID_NoiseFloor, false, defaultLabel[1]);
         UpdateControl(ID_Param1, true, _("Distortion amount"));
         UpdateControl(ID_Param2, true, _("Output level"));
         UpdateControl(ID_Repeats, true, _("Repeat processing"));
         UpdateControl(ID_DCBlock, false, wxEmptyString);
         break;
      case kEvenHarmonics:
         UpdateControlText(mThresholdT, mOldThresholdTxt, false);
         UpdateControlText(mNoiseFloorT, mOldmNoiseFloorTxt, false);
         UpdateControlText(mParam1T, mOldParam1Txt, true);
         UpdateControlText(mParam2T, mOldParam2Txt, true);
         UpdateControlText(mRepeatsT, mOldRepeatsTxt, false);

         UpdateControl(ID_Threshold, false, defaultLabel[0]);
         UpdateControl(ID_NoiseFloor, false, defaultLabel[1]);
         UpdateControl(ID_Param1, true, _("Distortion amount"));
         UpdateControl(ID_Param2, true, _("Harmonic brightness"));
         UpdateControl(ID_Repeats, false, defaultLabel[4]);
         UpdateControl(ID_DCBlock, true, wxEmptyString);
         break;
      case kSinCurve:
         UpdateControlText(mThresholdT, mOldThresholdTxt, false);
         UpdateControlText(mNoiseFloorT, mOldmNoiseFloorTxt, false);
         UpdateControlText(mParam1T, mOldParam1Txt, true);
         UpdateControlText(mParam2T, mOldParam2Txt, true);
         UpdateControlText(mRepeatsT, mOldRepeatsTxt, false);

         UpdateControl(ID_Threshold, false, defaultLabel[0]);
         UpdateControl(ID_NoiseFloor, false, defaultLabel[1]);
         UpdateControl(ID_Param1, true, _("Distortion amount"));
         UpdateControl(ID_Param2, true, _("Output level"));
         UpdateControl(ID_Repeats, false, defaultLabel[4]);
         UpdateControl(ID_DCBlock, false, wxEmptyString);
         break;
      case kLeveller:
         UpdateControlText(mThresholdT, mOldThresholdTxt, false);
         UpdateControlText(mNoiseFloorT, mOldmNoiseFloorTxt, true);
         UpdateControlText(mParam1T, mOldParam1Txt, true);
         UpdateControlText(mParam2T, mOldParam2Txt, false);
         UpdateControlText(mRepeatsT, mOldRepeatsTxt, true);

         UpdateControl(ID_Threshold, false, defaultLabel[0]);
         UpdateControl(ID_NoiseFloor, true, defaultLabel[1]);
         UpdateControl(ID_Param1, true, _("Levelling fine adjustment"));
         UpdateControl(ID_Param2, false, defaultLabel[3]);
         UpdateControl(ID_Repeats, true, _("Degree of Levelling"));
         UpdateControl(ID_DCBlock, false, wxEmptyString);
         break;
      case kRectifier:
         UpdateControlText(mThresholdT, mOldThresholdTxt, false);
         UpdateControlText(mNoiseFloorT, mOldmNoiseFloorTxt, false);
         UpdateControlText(mParam1T, mOldParam1Txt, true);
         UpdateControlText(mParam2T, mOldParam2Txt, false);
         UpdateControlText(mRepeatsT, mOldRepeatsTxt, false);

         UpdateControl(ID_Threshold, false, defaultLabel[0]);
         UpdateControl(ID_NoiseFloor, false, defaultLabel[1]);
         UpdateControl(ID_Param1, true, _("Distortion amount"));
         UpdateControl(ID_Param2, false, defaultLabel[3]);
         UpdateControl(ID_Repeats, false, defaultLabel[4]);
         UpdateControl(ID_DCBlock, true, wxEmptyString);
         break;
      case kHardLimiter:
         UpdateControlText(mThresholdT, mOldThresholdTxt, true);
         UpdateControlText(mNoiseFloorT, mOldmNoiseFloorTxt, false);
         UpdateControlText(mParam1T, mOldParam1Txt, true);
         UpdateControlText(mParam2T, mOldParam2Txt, true);
         UpdateControlText(mRepeatsT, mOldRepeatsTxt, false);

         UpdateControl(ID_Threshold, true, _("dB Limit"));
         UpdateControl(ID_NoiseFloor, false, defaultLabel[1]);
         UpdateControl(ID_Param1, true, _("Wet level"));
         UpdateControl(ID_Param2, true, _("Residual level"));
         UpdateControl(ID_Repeats, false, defaultLabel[4]);
         UpdateControl(ID_DCBlock, false, wxEmptyString);
         break;
      default:
         UpdateControl(ID_Threshold,   true, defaultLabel[0]);
         UpdateControl(ID_NoiseFloor,  true, defaultLabel[1]);
         UpdateControl(ID_Param1,      true, defaultLabel[2]);
         UpdateControl(ID_Param2,      true, defaultLabel[3]);
         UpdateControl(ID_Repeats,     true, defaultLabel[4]);
         UpdateControl(ID_DCBlock,     false, wxEmptyString);
   }
}

void EffectDistortion::UpdateControl(control id, bool enabled, wxString name)
{
   wxString suffix = _(" (Not Used):");
   switch (id)
   {
      case ID_Threshold:
         /* i18n-hint: Control range. */
         if (enabled) suffix = _(" (-100 to 0 dB):");
         name += suffix;

         // Logarithmic slider is set indirectly
         mThreshold = DB_TO_LINEAR(mParams.mThreshold_dB);
         mThresholdS->SetValue((int) (mThreshold * SCL_Threshold_dB + 0.5));

         mThresholdTxt->SetLabel(name);
         mThresholdS->SetName(name);
         mThresholdT->SetName(name);
         mThresholdS->Enable(enabled);
         mThresholdT->Enable(enabled);
         break;
      case ID_NoiseFloor:
         /* i18n-hint: Control range. */
         if (enabled) suffix = _(" (-80 to -20 dB):");
         name += suffix;

         mNoiseFloorTxt->SetLabel(name);
         mNoiseFloorS->SetName(name);
         mNoiseFloorT->SetName(name);
         mNoiseFloorS->Enable(enabled);
         mNoiseFloorT->Enable(enabled);
         break;
      case ID_Param1:
         /* i18n-hint: Control range. */
         if (enabled) suffix = _(" (0 to 100):");
         name += suffix;

         mParam1Txt->SetLabel(name);
         mParam1S->SetName(name);
         mParam1T->SetName(name);
         mParam1S->Enable(enabled);
         mParam1T->Enable(enabled);
         break;
      case ID_Param2:
         /* i18n-hint: Control range. */
         if (enabled) suffix = _(" (0 to 100):");
         name += suffix;

         mParam2Txt->SetLabel(name);
         mParam2S->SetName(name);
         mParam2T->SetName(name);
         mParam2S->Enable(enabled);
         mParam2T->Enable(enabled);
         break;
      case ID_Repeats:
         /* i18n-hint: Control range. */
         if (enabled) suffix = _(" (0 to 5):");
         name += suffix;

         mRepeatsTxt->SetLabel(name);
         mRepeatsS->SetName(name);
         mRepeatsT->SetName(name);
         mRepeatsS->Enable(enabled);
         mRepeatsT->Enable(enabled);
         break;
      case ID_DCBlock:
         if (enabled) {
            mDCBlockCheckBox->SetValue(mbSavedFilterState);
            mParams.mDCBlock = mbSavedFilterState;
         }
         else {
            mDCBlockCheckBox->SetValue(false);
            mParams.mDCBlock = false;
         }

         mDCBlockCheckBox->Enable(enabled);
         break;
      default: break;
   }
}

void EffectDistortion::UpdateControlText(wxTextCtrl* textCtrl, wxString& string, bool enabled)
{
   if (enabled) {
      if (textCtrl->GetValue() == wxEmptyString)
         textCtrl->SetValue(string);
      else
         string = textCtrl->GetValue();
   }
   else {
      if (textCtrl->GetValue() != wxEmptyString)
         string = textCtrl->GetValue();
      textCtrl->SetValue(wxT(""));
   }
}

void EffectDistortion::MakeTable()
{
   switch (mParams.mTableChoiceIndx)
   {
      case kHardClip:
         HardClip();
         break;
      case kSoftClip:
         SoftClip();
         break;
      case kHalfSinCurve:
         HalfSinTable();
         break;
      case kExpCurve:
         ExponentialTable();
         break;
      case kLogCurve:
         LogarithmicTable();
         break;
      case kCubic:
         CubicTable();
         break;
      case kEvenHarmonics:
         EvenHarmonicTable();
         break;
      case kSinCurve:
         SineTable();
         break;
      case kLeveller:
         Leveller();
         break;
      case kRectifier:
         Rectifier();
         break;
      case kHardLimiter:
         HardLimiter();
         break;
   }
}


//
// Preset tables for gain lookup
//

void EffectDistortion::HardClip()
{
   double lowThresh = 1 - mThreshold;
   double highThresh = 1 + mThreshold;

   for (int n = 0; n < TABLESIZE; n++) {
      if (n < (STEPS * lowThresh))
         mTable[n] = - mThreshold;
      else if (n > (STEPS * highThresh))
         mTable[n] = mThreshold;
      else
         mTable[n] = n/(double)STEPS - 1;

      mMakeupGain = 1.0 / mThreshold;
   }
}

void EffectDistortion::SoftClip()
{
   double threshold = 1 + mThreshold;
   double amount = std::pow(2.0, 7.0 * mParams.mParam1 / 100.0); // range 1 to 128
   double peak = LogCurve(mThreshold, 1.0, amount);
   mMakeupGain = 1.0 / peak;
   mTable[STEPS] = 0.0;   // origin

   // positive half of table
   for (int n = STEPS; n < TABLESIZE; n++) {
      if (n < (STEPS * threshold)) // origin to threshold
         mTable[n] = n/(float)STEPS - 1;
      else
         mTable[n] = LogCurve(mThreshold, n/(double)STEPS - 1, amount);
   }
   CopyHalfTable();
}

float EffectDistortion::LogCurve(double threshold, float value, double ratio)
{
   return threshold + ((std::exp(ratio * (threshold - value)) - 1) / -ratio);
}

void EffectDistortion::ExponentialTable()
{
   double amount = std::min(0.999, DB_TO_LINEAR(-1 * mParams.mParam1));   // avoid divide by zero

   for (int n = STEPS; n < TABLESIZE; n++) {
      double linVal = n/(float)STEPS;
      double scale = -1.0 / (1.0 - amount);   // unity gain at 0dB
      double curve = std::exp((linVal - 1) * std::log(amount));
      mTable[n] = scale * (curve -1);
   }
   CopyHalfTable();
}

void EffectDistortion::LogarithmicTable()
{
   double amount = mParams.mParam1;
   double stepsize = 1.0 / STEPS;
   double linVal = 0;

   if (amount == 0){
      for (int n = STEPS; n < TABLESIZE; n++) {
      mTable[n] = linVal;
      linVal += stepsize;
      }
   }
   else {
      for (int n = STEPS; n < TABLESIZE; n++) {
         mTable[n] = std::log(1 + (amount * linVal)) / std::log(1 + amount);
         linVal += stepsize;
      }
   }
   CopyHalfTable();
}

void EffectDistortion::HalfSinTable()
{
   int iter = std::floor(mParams.mParam1 / 20.0);
   double fractionalpart = (mParams.mParam1 / 20.0) - iter;
   double stepsize = 1.0 / STEPS;
   double linVal = 0;

   for (int n = STEPS; n < TABLESIZE; n++) {
      mTable[n] = linVal;
      for (int i = 0; i < iter; i++) {
         mTable[n] = std::sin(mTable[n] * M_PI_2);
      }
      mTable[n] += ((std::sin(mTable[n] * M_PI_2) - mTable[n]) * fractionalpart);
      linVal += stepsize;
   }
   CopyHalfTable();
}

void EffectDistortion::CubicTable()
{
   double amount = mParams.mParam1 * std::sqrt(3.0) / 100.0;
   double gain = 1.0;
   if (amount != 0.0)
      gain = 1.0 / Cubic(std::min(amount, 1.0));

   double stepsize = amount / STEPS;
   double x = -amount;
   
   if (amount == 0) {
      for (int i = 0; i < TABLESIZE; i++) {
         mTable[i] = (i / (double)STEPS) - 1.0;
      }
   }
   else {
      for (int i = 0; i < TABLESIZE; i++) {
         mTable[i] = gain * Cubic(x);
         for (int j = 0; j < mParams.mRepeats; j++) {
            mTable[i] = gain * Cubic(mTable[i] * amount);
         }
         x += stepsize;
      }
   }
}

double EffectDistortion::Cubic(double x)
{
   if (mParams.mParam1 == 0.0)
      return x;

   return x - (std::pow(x, 3.0) / 3.0);
}


void EffectDistortion::EvenHarmonicTable()
{
   double amount = mParams.mParam1 / -100.0;
   // double C = std::sin(std::max(0.001, mParams.mParam2) / 100.0) * 10.0;
   double C = std::max(0.001, mParams.mParam2) / 10.0;

   double step = 1.0 / STEPS;
   double xval = -1.0;

   for (int i = 0; i < TABLESIZE; i++) {
      mTable[i] = ((1 + amount) * xval) -
                  (xval * (amount / std::tanh(C)) * std::tanh(C * xval));
      xval += step;
   }
}

void EffectDistortion::SineTable()
{
   int iter = std::floor(mParams.mParam1 / 20.0);
   double fractionalpart = (mParams.mParam1 / 20.0) - iter;
   double stepsize = 1.0 / STEPS;
   double linVal = 0.0;

   for (int n = STEPS; n < TABLESIZE; n++) {
      mTable[n] = linVal;
      for (int i = 0; i < iter; i++) {
         mTable[n] = (1.0 + std::sin((mTable[n] * M_PI) - M_PI_2)) / 2.0;
      }
      mTable[n] += (((1.0 + std::sin((mTable[n] * M_PI) - M_PI_2)) / 2.0) - mTable[n]) * fractionalpart;
      linVal += stepsize;
   }
   CopyHalfTable();
}

void EffectDistortion::Leveller()
{
   double noiseFloor = DB_TO_LINEAR(mParams.mNoiseFloor);
   int numPasses = mParams.mRepeats;
   double fractionalPass = mParams.mParam1 / 100.0;

   const int numPoints = 6;
   const double gainFactors[numPoints] = { 0.80, 1.00, 1.20, 1.20, 1.00, 0.80 };
   double gainLimits[numPoints] = { 0.0001, 0.0, 0.1, 0.3, 0.5, 1.0 };
   double addOnValues[numPoints];

   gainLimits[1] = noiseFloor;
   /* In the original Leveller effect, behaviour was undefined for threshold > 20 dB.
    * If we want to support > 20 dB we need to scale the points to keep them non-decreasing.
    * 
    * if (noiseFloor > gainLimits[2]) {
    *    for (int i = 3; i < numPoints; i++) {
    *    gainLimits[i] = noiseFloor + ((1 - noiseFloor)*((gainLimits[i] - 0.1) / 0.9));
    * }
    * gainLimits[2] = noiseFloor;
    * }
    */

   // Calculate add-on values
   addOnValues[0] = 0.0;
   for (int i = 0; i < numPoints-1; i++) {
      addOnValues[i+1] = addOnValues[i] + (gainLimits[i] * (gainFactors[i] - gainFactors[1 + i]));
   }

   // Positive half of table.
   // The original effect increased the 'strength' of the effect by
   // repeated passes over the audio data.
   // Here we model that more efficiently by repeated passes over a linear table.
   for (int n = STEPS; n < TABLESIZE; n++) {
      mTable[n] = ((double) (n - STEPS) / (double) STEPS);
      for (int i = 0; i < numPasses; i++) {
         // Find the highest index for gain adjustment
         int index = numPoints - 1;
         for (int i = index; i >= 0 && mTable[n] < gainLimits[i]; i--) {
            index = i;
         }
         // the whole number of 'repeats'
         mTable[n] = (mTable[n] * gainFactors[index]) + addOnValues[index];
      }
      // Extrapolate for fine adjustment.
      // tiny fractions are not worth the processing time
      if (fractionalPass > 0.001) {
         int index = numPoints - 1;
         for (int i = index; i >= 0 && mTable[n] < gainLimits[i]; i--) {
            index = i;
         }
         mTable[n] += fractionalPass * ((mTable[n] * (gainFactors[index] - 1)) + addOnValues[index]);
      }
   }
   CopyHalfTable();
}

void EffectDistortion::Rectifier()
{
   double amount = (mParams.mParam1 / 50.0) - 1;
   double stepsize = 1.0 / STEPS;
   int index = STEPS;

   // positive half of waveform is passed unaltered.
   for  (int n = 0; n <= STEPS; n++) {
      mTable[index] = n * stepsize;
      index += 1;
   }

   // negative half of table
   index = STEPS - 1;
   for (int n = 1; n <= STEPS; n++) {
      mTable[index] = n * stepsize * amount;
      index--;
   }
}

void EffectDistortion::HardLimiter()
{
   // The LADSPA "hardLimiter 1413" is basically hard clipping,
   // but with a 'kind of' wet/dry mix:
   // out = ((wet-residual)*clipped) + (residual*in)
   HardClip();
}


// Helper functions for lookup tables

void EffectDistortion::CopyHalfTable()
{
   // Copy negative half of table from positive half
   int count = TABLESIZE - 1;
   for (int n = 0; n < STEPS; n++) {
      mTable[n] = -mTable[count];
      count--;
   }
}


float EffectDistortion::WaveShaper(float sample)
{
   float out;
   int index;
   double xOffset;
   double amount = 1;

   switch (mParams.mTableChoiceIndx)
   {
      // Do any pre-processing here
      case kHardClip:
         // Pre-gain
         amount = mParams.mParam1 / 100.0;
         sample *= 1+amount;
         break;
      default: break;
   }

   index = std::floor(sample * STEPS) + STEPS;
   index = wxMax<int>(wxMin<int>(index, 2 * STEPS - 1), 0);
   xOffset = ((1 + sample) * STEPS) - index;
   xOffset = wxMin<double>(wxMax<double>(xOffset, 0.0), 1.0);   // Clip at 0dB

   // linear interpolation: y = y0 + (y1-y0)*(x-x0)
   out = mTable[index] + (mTable[index + 1] - mTable[index]) * xOffset;

   return out;
}


float EffectDistortion::DCFilter(EffectDistortionState& data, float sample)
{
   // Rolling average gives less offset at the start than an IIR filter.
   const unsigned int queueLength = std::floor(data.samplerate / 20.0);

   data.queuetotal += sample;
   data.queuesamples.push(sample);

   if (data.queuesamples.size() > queueLength) {
      data.queuetotal -= data.queuesamples.front();
      data.queuesamples.pop();
   }

   return sample - (data.queuetotal / data.queuesamples.size());
}
