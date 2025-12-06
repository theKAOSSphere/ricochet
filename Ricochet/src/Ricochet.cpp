#include <stdlib.h>
#include <cmath>
#include <algorithm>
#include "PitchShifterClasses.h"
#include "GainClass.h"

/**********************************************************************************************************************************************************/

#define PLUGIN_URI "https://github.com/theKAOSSphere/ricochet"
#define FIDELITY0 6,3,2,1
#define FIDELITY1 12,6,3,2
#define FIDELITY2 16,8,4,2
#define FIDELITY3 20,10,5,3
#define FIDELITY4 32,16,8,4
#define FIDELITY5 48,24,12,6
enum {IN, OUT, TRIGGER, MODE, INTERVAL, DIRECTION, SHIFT_TIME, RETURN_TIME, CLEAN, WET_GAIN, FIDELITY, TRUE_BYPASS, PLUGIN_PORT_COUNT};

namespace
{
    struct IntervalChoice
    {
        double semitones;
        bool force_dry;
    };

    static const IntervalChoice kIntervalChoices[] = {
        {0.0, false},   // Unison
        {2.0, false},   // Major second
        {5.0, false},   // Perfect fourth
        {7.0, false},   // Perfect fifth
        {11.0, false},  // Major seventh
        {12.0, false},  // Octave
        {24.0, false},  // Double octave
        {12.0, true}    // Octave + Dry
    };

    constexpr size_t kIntervalChoiceCount = sizeof(kIntervalChoices) / sizeof(kIntervalChoices[0]);
    constexpr double kTimeEpsilon = 1e-9;
}

/**********************************************************************************************************************************************************/

class Ricochet
{
public:
    Ricochet(uint32_t n_samples, int nBuffers, double samplerate, const std::string& wfile)
    {
        wisdomFile = wfile;
        Construct(n_samples, nBuffers, samplerate, wfile.c_str());
    }

    ~Ricochet(){Destruct();}
    
    void Construct(uint32_t n_samples, int nBuffers, double samplerate, const char* wisdomFile)
    {
        this->nBuffers = nBuffers;
        SampleRate = samplerate;

        obja = new PSAnalysis(n_samples, nBuffers, wisdomFile);
        objs = new PSSinthesis(obja, wisdomFile);
        objg = new GainClass(n_samples);

        last_out = new float[n_samples];
        
        // Variables to handle crossfading during true bypass
        fade_progress = 0.0;
        fade_step = 1.0 / (0.020 * SampleRate); 

        cont = 0;
        current_s = 0.0;
        ramp_position = 0.0;
        ramp_target = 0.0;
        ramp_samples_remaining = 0.0;
        ramp_step = 0.0;
        ramp_active_time = 0.0;
        latched_on = false;
        prev_trigger_state = false;
        last_mode_was_latch = false;
        auto_add_dry = false;
    }
    
    void Destruct()
    {
        delete obja;
        delete objs;
        delete objg;
        delete[] last_out;
    }
    
    void Realloc(uint32_t n_samples, int nBuffers)
    {
        Destruct();
        Construct(n_samples, nBuffers, SampleRate, wisdomFile.c_str());
    }

    void SetFidelity(int fidelity, uint32_t n_samples)
    {
        int bufsize;

        switch (fidelity)
        {
            case 0: 
                bufsize = nBuffersSW(n_samples,FIDELITY0);
                break;
            case 1:
                bufsize = nBuffersSW(n_samples,FIDELITY1);
                break;
            case 2:
                bufsize = nBuffersSW(n_samples,FIDELITY2);
                break;
            case 3:
                bufsize = nBuffersSW(n_samples,FIDELITY3);
                break;
            case 4:
                bufsize = nBuffersSW(n_samples,FIDELITY4);
                break;
            case 5:
                bufsize = nBuffersSW(n_samples,FIDELITY5);
                break;
            default:
                return;
        }

        if (nBuffers != bufsize || obja->hopa != (int)n_samples)
            Realloc(n_samples, bufsize);
    }

    static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double samplerate, const char* bundle_path, const LV2_Feature* const* features);
    static void activate(LV2_Handle instance);
    static void deactivate(LV2_Handle instance);
    static void connect_port(LV2_Handle instance, uint32_t port, void *data);
    static void run(LV2_Handle instance, uint32_t n_samples);
    static void cleanup(LV2_Handle instance);
    static const void* extension_data(const char* uri);
    double UpdateStep(bool trigger_active,
                      bool latch_mode,
                      double interval_control,
                      bool direction_up,
                      double shift_time,
                      double return_time,
                      uint32_t n_samples);
    float *ports[PLUGIN_PORT_COUNT];
    
    PSAnalysis *obja;
    PSSinthesis *objs;
    GainClass *objg;

    int nBuffers;
    int cont;
    double SampleRate;
    std::string wisdomFile;
    double current_s;
    double ramp_position;
    double ramp_target;
    double ramp_samples_remaining;
    double ramp_step;
    double ramp_active_time;
    bool latched_on;
    bool prev_trigger_state;
    bool last_mode_was_latch;
    bool auto_add_dry;
    bool engaged;
    bool was_true_bypassing;
    bool fading_in;
    bool fading_out;
    bool prev_engaged;
    float* last_out;
    double prev_ramp_samples_remaining;
    double fade_progress;
    double fade_step;
};

/**********************************************************************************************************************************************************/

static const LV2_Descriptor Descriptor = {
    PLUGIN_URI,
    Ricochet::instantiate,
    Ricochet::connect_port,
    Ricochet::activate,
    Ricochet::run,
    Ricochet::deactivate,
    Ricochet::cleanup,
    Ricochet::extension_data
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index)
{
    if (index == 0) return &Descriptor;
    else return NULL;
}

/**********************************************************************************************************************************************************/

LV2_Handle Ricochet::instantiate(const LV2_Descriptor* descriptor, double samplerate, const char* bundle_path, const LV2_Feature* const* features)
{
    std::string wisdomFile = bundle_path;
    wisdomFile += "/harmonizer.wisdom";
    const uint32_t n_samples = GetBufferSize(features);
    Ricochet *plugin = new Ricochet(n_samples, nBuffersSW(n_samples,FIDELITY1), samplerate, wisdomFile);
    return (LV2_Handle)plugin;
}

/**********************************************************************************************************************************************************/

void Ricochet::activate(LV2_Handle instance)
{
    Ricochet *plugin = (Ricochet *)instance;
    plugin->current_s = 0.0;
    plugin->ramp_position = 0.0;
    plugin->ramp_target = 0.0;
    plugin->ramp_samples_remaining = 0.0;
    plugin->ramp_step = 0.0;
    plugin->ramp_active_time = 0.0;
    plugin->latched_on = false;
    plugin->prev_trigger_state = false;
    plugin->last_mode_was_latch = false;
    plugin->auto_add_dry = false;
    plugin->engaged = false;
    plugin->was_true_bypassing = false;
    plugin->fading_in = false;
    plugin->fading_out = false;
    plugin->prev_engaged = false;
    plugin->prev_ramp_samples_remaining = 0.0;
    (plugin->objs)->ClearBuffers();
    plugin->cont = 0;
    plugin->fade_progress = 0.0;
}

/**********************************************************************************************************************************************************/

void Ricochet::deactivate(LV2_Handle instance){}

/**********************************************************************************************************************************************************/

void Ricochet::connect_port(LV2_Handle instance, uint32_t port, void *data)
{
    Ricochet *plugin;
    plugin = (Ricochet *) instance;
    plugin->ports[port] = (float*) data;
}

/**********************************************************************************************************************************************************/

void Ricochet::run(LV2_Handle instance, uint32_t n_samples)
{
    Ricochet *plugin = (Ricochet *) instance;

    float *in           = plugin->ports[IN];
    float *out          = plugin->ports[OUT];
    bool   trigger      = (*(plugin->ports[TRIGGER]) >= 0.5f);
    bool   latch        = (*(plugin->ports[MODE])    >= 0.5f);
    double interval     = (double)(*(plugin->ports[INTERVAL]));
    bool   up           = (*(plugin->ports[DIRECTION]) >= 0.5f);
    double shift        = std::max(0.0, (double)(*(plugin->ports[SHIFT_TIME])));
    double retrn        = std::max(0.0, (double)(*(plugin->ports[RETURN_TIME])));
    double wet_gain     = (double)(*(plugin->ports[WET_GAIN]));
    int    clean        = (int)(*(plugin->ports[CLEAN])+0.5f);
    int    fidelity     = (int)(*(plugin->ports[FIDELITY])+0.5f);
    bool   true_bypass  = (*(plugin->ports[TRUE_BYPASS]) >= 0.5f);

    plugin->SetFidelity(fidelity, n_samples);
    double semitone = plugin->UpdateStep(trigger, latch, interval, up, shift, retrn, n_samples);

    // --- STATE MACHINE FOR BYPASS LOGIC ---

    // 1. Detect Pitch Return Completion
    // When ramp hits 0, the pitch effect is effectively "off", but processing is running.
    bool ramp_just_finished = (plugin->ramp_samples_remaining == 0.0) && (plugin->prev_ramp_samples_remaining > 0.0);
    plugin->prev_ramp_samples_remaining = plugin->ramp_samples_remaining;

    // 2. Handle Engagement (Start Fade In)
    if (true_bypass && plugin->engaged && !plugin->prev_engaged) 
    {
        plugin->fading_in = true;
        plugin->fading_out = false;
        plugin->fade_progress = 0.0;
        // CRITICAL FIX: Clear buffers on engage. Do not "Prime" them with SetYShiftFromInput.
        // Starting from zero buffers means the wet signal fades in from silence = No Pop.
        (plugin->objs)->ClearBuffers(); 
        plugin->cont = 0; 
        plugin->was_true_bypassing = false;
    }
    
    // 3. Handle Disengagement (Start Fade Out)
    // Only fade out if true bypass is enabled, we aren't engaged, and pitch has returned to 0.
    if (true_bypass && !plugin->engaged && ramp_just_finished && plugin->ramp_target == 0.0) 
    {
        plugin->fading_out = true;
        plugin->fading_in = false;
        plugin->fade_progress = 0.0;
    }
    
    plugin->prev_engaged = plugin->engaged;

    // --- PROCESSING ---

    // Optimization: If hard bypassed and stable (not fading), copy and return.
    if (true_bypass && !plugin->fading_in && !plugin->fading_out && !plugin->engaged 
        && plugin->ramp_position == 0.0 && plugin->ramp_samples_remaining == 0.0) 
    {
        memcpy(out, in, n_samples * sizeof(float));
        plugin->was_true_bypassing = true; // Mark that we have been sleeping
        return;
    }

    // Standard Processing Logic
    (plugin->obja)->PreAnalysis(plugin->nBuffers, in);
    (plugin->objs)->PreSinthesis();

    // Safety: If input is silent, output silence (saves CPU on denormals)
    if (InputAbsSum(in, n_samples) == 0)
    {
        memset(out,0,sizeof(float)*n_samples);
        return;
    }

    (plugin->objg)->SetGaindB(wet_gain);

    bool processed = false;
    if (plugin->cont < plugin->nBuffers-1)
    {
        plugin->cont = plugin->cont + 1;
    }
    else
    {
        (plugin->obja)->Analysis();
        (plugin->objs)->Sinthesis(semitone);
        (plugin->objg)->SimpleGain((plugin->objs)->yshift, out);
        if (plugin->auto_add_dry || clean == 1)
        {
            const double *dry = (plugin->obja)->frames;
            for (uint32_t i = 0; i<n_samples; ++i)
                out[i] += static_cast<float>(dry[i]);
        }
        processed = true;
    }

    // --- CROSSFADE LOGIC ---
    
    if (processed) 
    {
        // Save current output for next block (in case we need it, though less critical now)
        memcpy(plugin->last_out, out, n_samples * sizeof(float));

        if (plugin->fading_in) 
        {
            // Fading FROM Input TO Wet
            for (uint32_t i = 0; i < n_samples; ++i) 
            {
                double f = std::min(1.0, plugin->fade_progress);
                // Linear Crossfade: Dry -> Wet
                out[i] = in[i] * (1.0f - f) + out[i] * f;
                plugin->fade_progress += plugin->fade_step;
            }
            if (plugin->fade_progress >= 1.0) plugin->fading_in = false;
        } 
        else if (plugin->fading_out) 
        {
            // Fading FROM Wet TO Input
            for (uint32_t i = 0; i < n_samples; ++i) 
            {
                double f = std::min(1.0, plugin->fade_progress);
                // Linear Crossfade: Wet -> Dry
                out[i] = out[i] * (1.0f - f) + in[i] * f;
                plugin->fade_progress += plugin->fade_step;
            }
            if (plugin->fade_progress >= 1.0) plugin->fading_out = false;
        }
    }
    else 
    {
        // Buffering phase (pitch shifter filling up), output silence or dry depending on state
        if (plugin->fading_in) 
        {
            // If we are fading in but DSP isn't ready, output dry to avoid silence gap
            memcpy(out, in, n_samples * sizeof(float));
        } 
        else 
        {
            memset(out, 0, n_samples * sizeof(float));
        }
    }
}

/**********************************************************************************************************************************************************/

void Ricochet::cleanup(LV2_Handle instance)
{
    delete ((Ricochet *) instance);
}

/**********************************************************************************************************************************************************/

const void* Ricochet::extension_data(const char* uri)
{
    return NULL;
}

double Ricochet::UpdateStep(bool trigger_active,
                            bool latch_mode,
                            double interval_control,
                            bool direction_up,
                            double shift_time,
                            double return_time,
                            uint32_t n_samples)
{
    double clamped = std::max(0.0, std::min(interval_control, static_cast<double>(kIntervalChoiceCount - 1)));
    size_t interval_index = static_cast<size_t>(clamped + 0.5);
    if (interval_index >= kIntervalChoiceCount)
        interval_index = kIntervalChoiceCount - 1;

    const IntervalChoice &choice = kIntervalChoices[interval_index];
    auto_add_dry = choice.force_dry;

    if (!latch_mode && last_mode_was_latch)
        latched_on = false;

    bool engaged;
    if (latch_mode)
    {
        if (trigger_active && !prev_trigger_state)
            latched_on = !latched_on;
        engaged = latched_on;
    }
    else
    {
        engaged = trigger_active;
        latched_on = engaged;
    }

    prev_trigger_state = trigger_active;
    last_mode_was_latch = latch_mode;

    this->engaged = engaged;

    double target = engaged ? (direction_up ? choice.semitones : -choice.semitones) : 0.0;
    double duration = engaged ? shift_time : return_time;

    // If pitch needs to change, enforce a minimum duration of 20ms to prevent a pop
    if (std::fabs(ramp_position - target) > 1e-9)
        duration = std::max(duration, 0.020);

    if (duration <= 0.0)
    {
        ramp_position = target;
        ramp_target = target;
        ramp_samples_remaining = 0.0;
        ramp_step = 0.0;
        ramp_active_time = 0.0;
        current_s = target;
        return current_s;
    }

    if (std::fabs(target - ramp_target) > 1e-9 ||
        std::fabs(duration - ramp_active_time) > kTimeEpsilon)
    {
        ramp_target = target;
        ramp_active_time = duration;
        ramp_samples_remaining = duration * SampleRate;
        if (ramp_samples_remaining < 1.0)
            ramp_samples_remaining = 1.0;
        ramp_step = (ramp_target - ramp_position) / ramp_samples_remaining;
    }

    double start = ramp_position;

    if (ramp_samples_remaining > 0.0)
    {
        double advance = std::min(ramp_samples_remaining, static_cast<double>(n_samples));
        ramp_position += ramp_step * advance;
        ramp_samples_remaining -= advance;

        if ((ramp_step >= 0.0 && ramp_position >= ramp_target) ||
            (ramp_step <= 0.0 && ramp_position <= ramp_target) ||
            ramp_samples_remaining <= 0.0)
        {
            ramp_position = ramp_target;
            ramp_samples_remaining = 0.0;
            ramp_step = 0.0;
        }
    }
    else
    {
        ramp_position = ramp_target;
    }

    double end = ramp_position;
    if (std::fabs(end - start) > 1e-12)
        current_s = 0.5 * (start + end);
    else
        current_s = end;

    return current_s;
}
