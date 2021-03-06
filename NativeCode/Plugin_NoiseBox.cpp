#include "AudioPluginUtil.h"

namespace NoiseBox
{
    enum Param
    {
        P_ADDAMT,
        P_MULAMT,
        P_ADDFREQ,
        P_MULFREQ,
        P_NUM
    };

    struct EffectData
    {
        struct Data
        {
            float p[P_NUM];
            float addcount;
            float addnoise;
            float mulcount;
            float mulnoise;
            Random random;
        };
        union
        {
            Data data;
            unsigned char pad[(sizeof(Data) + 15) & ~15]; // This entire structure must be a multiple of 16 bytes (and and instance 16 byte aligned) for PS3 SPU DMA requirements
        };
    };

#if !UNITY_SPU

    int InternalRegisterEffectDefinition(UnityAudioEffectDefinition& definition)
    {
        int numparams = P_NUM;
        definition.paramdefs = new UnityAudioParameterDefinition[numparams];
        RegisterParameter(definition, "Add Amount", "dB", -100.0f, 0.0f, -50.0f, 1.0f, 1.0f, P_ADDAMT, "Gain of additive noise in dB");
        RegisterParameter(definition, "Mul Amount", "dB", -100.0f, 0.0f, -50.0f, 1.0f, 1.0f, P_MULAMT, "Gain of multiplicative noise in dB");
        RegisterParameter(definition, "Add Frequency", "Hz", 0.001f, 24000.0f, 5000.0f, 1.0f, 3.0f, P_ADDFREQ, "Additive noise frequency cutoff in Hz");
        RegisterParameter(definition, "Mul Frequency", "Hz", 0.001f, 24000.0f, 50.0f, 1.0f, 3.0f, P_MULFREQ, "Multiplicative noise frequency cutoff in Hz");
        return numparams;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK CreateCallback(UnityAudioEffectState* state)
    {
        EffectData* effectdata = new EffectData;
        memset(effectdata, 0, sizeof(EffectData));
        state->effectdata = effectdata;
        InitParametersFromDefinitions(InternalRegisterEffectDefinition, effectdata->data.p);
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ReleaseCallback(UnityAudioEffectState* state)
    {
        EffectData::Data* data = &state->GetEffectData<EffectData>()->data;
        delete data;
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK SetFloatParameterCallback(UnityAudioEffectState* state, int index, float value)
    {
        EffectData::Data* data = &state->GetEffectData<EffectData>()->data;
        if (index >= P_NUM)
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        data->p[index] = value;
        return UNITY_AUDIODSP_OK;
    }

    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK GetFloatParameterCallback(UnityAudioEffectState* state, int index, float* value, char *valuestr)
    {
        EffectData::Data* data = &state->GetEffectData<EffectData>()->data;
        if (index >= P_NUM)
            return UNITY_AUDIODSP_ERR_UNSUPPORTED;
        if (value != NULL)
            *value = data->p[index];
        if (valuestr != NULL)
            valuestr[0] = 0;
        return UNITY_AUDIODSP_OK;
    }

    int UNITY_AUDIODSP_CALLBACK GetFloatBufferCallback(UnityAudioEffectState* state, const char* name, float* buffer, int numsamples)
    {
        return UNITY_AUDIODSP_OK;
    }

#endif

#if !UNITY_PS3 || UNITY_SPU

#if UNITY_SPU
    EffectData  g_EffectData __attribute__((aligned(16)));
    extern "C"
#endif
    UNITY_AUDIODSP_RESULT UNITY_AUDIODSP_CALLBACK ProcessCallback(UnityAudioEffectState* state, float* inbuffer, float* outbuffer, unsigned int length, int inchannels, int outchannels)
    {
        EffectData::Data* data = &state->GetEffectData<EffectData>()->data;

#if UNITY_SPU
        UNITY_PS3_CELLDMA_GET(&g_EffectData, state->effectdata, sizeof(g_EffectData));
        data = &g_EffectData.data;
#endif

        if ((state->flags & UnityAudioEffectStateFlags_IsPlaying) == 0 || (state->flags & (UnityAudioEffectStateFlags_IsMuted | UnityAudioEffectStateFlags_IsPaused)) != 0)
        {
            memcpy(outbuffer, inbuffer, sizeof(float) * length * inchannels);
            return UNITY_AUDIODSP_OK;
        }

        const float addperiod = state->samplerate * 0.5f / data->p[P_ADDFREQ];
        const float mulperiod = state->samplerate * 0.5f / data->p[P_MULFREQ];
        const float addgain = powf(10.0f, 0.05f * data->p[P_ADDAMT]);
        const float mulgain = powf(10.0f, 0.05f * data->p[P_MULAMT]);

        for (unsigned int n = 0; n < length; n++)
        {
            for (int i = 0; i < outchannels; i++)
            {
                outbuffer[n * outchannels + i] = inbuffer[n * outchannels + i] * (1.0f - mulgain + mulgain * data->mulnoise) + addgain * data->addnoise;
            }
            data->addcount += 1.0f; if (data->addcount >= addperiod) { data->addcount -= addperiod; data->addnoise = data->random.GetFloat(-1.0, 1.0f); }
            data->mulcount += 1.0f; if (data->mulcount >= addperiod) { data->mulcount -= mulperiod; data->mulnoise = data->random.GetFloat(0.0, 1.0f); }
        }

#if UNITY_SPU
        UNITY_PS3_CELLDMA_PUT(&g_EffectData, state->effectdata, sizeof(g_EffectData));
#endif

        return UNITY_AUDIODSP_OK;
    }

#endif
}
