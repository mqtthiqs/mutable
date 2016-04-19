#include "stmlib/dsp/units.h"
#include "clouds/resources.h"

#ifndef CLOUDS_DSP_CHORDS_H_
#define CLOUDS_DSP_CHORDS_H_

using namespace stmlib;

namespace clouds {

  
  enum ModulationType {
    FM,
    AM,
  };

  const float a3 = 440.0f / 32000;

  const int kNumVoices = 6;

  const float chords_table[12][6/*kNumVoices*/] = {
    { 0.0f, 0.01f, 5.0f, 5.01f, 11.99f, 12.0f},
    { 0.0f, 0.01f, 3.0f, 3.01f, 10.0f, 12.0f},
    { 0.0f, 3.0f, 5.0f, 5.01f, 11.99f, 12.0f},
    { 0.0f, 2.01f, 3.0f, 7.01f, 10.0f, 12.0f},
    { 0.0f, 1.0f, 3.0f, 5.0f, 11.0f, 12.0f},
    { 0.0f, 0.01f, 4.0f, 7.0f, 11.01f, 12.0f},
    { 0.0f, 0.01f, 3.0f, 6.01f, 8.99f, 12.0f},
    { 0.0f, 1.0f, 3.0f, 5.0f, 8.0f, 12.0f},
    { 0.0f, 4.01f, 5.0f, 8.0f, 11.0f, 12.0f},
    { 0.0f, 1.0f, 2.0f, 3.01f, 4.0f, 5.0f},
    { 0.0f, 2.0f, 4.0f, 6.0f, 8.0f, 10.0f},
  };

  const int kNumStructures = 8;

  const float modulation_table[6/* kNumVoices */][kNumStructures] = {
    {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f},
    {0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
    {0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
  };

  class Chords {
  public:
    Chords() { }
    ~Chords() { }

    void Init() {
      for (int i=0; i<kNumVoices; i++) {
        phase_[i] = 0.0f;
      }
      bitcrush_ = decimate_ = 65535.0f;
      softclip_ = 0.0001f;
    }

    template<ModulationType modulation_type>
    void Process(FloatFrame* in_out, size_t size) {

      modulation_sample_[0] = 1.0f;
      modulation_sample_[0] = 1.0f;

      while (size--) {

        float in_l = in_out->l;
        float in_r = in_out->r;

        in_out->l = in_out->r = 0.0f;

        float total_gain = 0.0f;

        for (int i=0; i<kNumVoices; i++) {

          float phase_l, phase_r;

          if (modulation_type == FM) {
            phase_l = phase_[i] + in_l
              + self_feedback_sample_[i][1] * self_feedback_
              + self_feedback_
              + modulation_sample_[i] * modulation_index_;
            phase_r = phase_[i] + in_r
              + self_feedback_sample_[i][3] * self_feedback_
              + self_feedback_
              + modulation_sample_[i] * modulation_index_;
            while (phase_l < 0.0f) phase_l++;
            while (phase_l > 1.0f) phase_l--;
            while (phase_r < 0.0f) phase_r++;
            while (phase_r > 1.0f) phase_r--;
          } else {
            phase_l = phase_r = phase_[i];
          }

          /* decimate */
          phase_l = floorf(phase_l * decimate_) / decimate_;
          phase_r = floorf(phase_r * decimate_) / decimate_;

          float sin = Interpolate(lut_sin, phase_l, 1024.0f);
          float cos = Interpolate(lut_sin + 256, phase_r, 1024.0f);

          /* bitcrush */
          sin = truncf(sin * bitcrush_) / bitcrush_;
          cos = truncf(cos * bitcrush_) / bitcrush_;

          /* softclip */
          sin = SoftLimit(sin * softclip_) / SoftLimit(softclip_);
          cos = SoftLimit(cos * softclip_) / SoftLimit(softclip_);

          if (modulation_type == AM) {
            float fb = 1.0f - (self_feedback_sample_[i][1] + 1.0f) * self_feedback_;
            sin *= modulation_sample_[i] + fb + in_l;
            cos *= modulation_sample_[i] + fb + in_r;
          }

          self_feedback_sample_[i][0] = sin;
          ONE_POLE(self_feedback_sample_[i][1], self_feedback_sample_[i][0], 0.1f);
          self_feedback_sample_[i][2] = cos;
          ONE_POLE(self_feedback_sample_[i][3], self_feedback_sample_[i][2], 0.1f);

          if (i != kNumVoices-1) {
            if (modulation_type == AM) {
              modulation_sample_[i+1] = 1.0f - (sin + 1.0f)
                * modulation_matrix_[i]
                * modulation_index_;
            } else if (modulation_type == FM) {
              modulation_sample_[i+1] = sin * modulation_matrix_[i];
            }
          }


          float gain = 1.0f - modulation_matrix_[i];
          total_gain += gain;

          in_out->l += sin * gain;
          in_out->r += cos * gain;

          phase_[i] += phase_increment_[i];
          if (phase_[i] > 1.0) phase_[i]--;
        }

        total_gain += 2.0f;

        if (modulation_type == FM) {
          in_out->l /= total_gain;
          in_out->r /= total_gain;
        } else if (modulation_type == AM) {
          in_out->l /= total_gain * 2.0f;
          in_out->r /= total_gain * 2.0f;
        }

        in_out++;
      }
    }

    void set_frequencies(float note, float spread, float fine, float distrib) {

      for (int i=kNumVoices-1; i>=0; i--) {
          if (i&1 || !freeze_)
            phase_increment_[i] = SemitonesToRatio(note + fine - 69.0f) * a3;

        note += spread;
      }
    }

    void set_chords(float note, float spread, float fine, float chord) {

      int ch = static_cast<int>(chord * 12.0f);

      for (int i=kNumVoices-1; i>=0; i--) {

        float note_q = note;

        int octaves = floorf(note / 12.0f) * 12.0f;
        note_q = binary_search(note - octaves, chords_table[ch], 6) + octaves;

        if (note_q + fine - 69.0f < 64.0f &&
            note_q + fine - 69.0f > -64.0f)
          if (i&1 || !freeze_)
            phase_increment_[i] = SemitonesToRatio(note_q + fine - 69.0f) * a3;

        note += spread;
      }
    }

    void set_rationals(float note, float spread, float fine, int max_denom) {

      float fundamental = SemitonesToRatio(45.0f + fine - 69.0f) * a3;

      float freq = fundamental * note;

      for (int i=kNumVoices-1; i>=0; i--) {

        float ratio = freq / fundamental; /* TODO what if < 1 */
        ratio = closest_rational(ratio, max_denom);
        if (i&1 || !freeze_)
          phase_increment_[i] = ratio * fundamental;

        freq *= spread;
      }
    }

    void set_harmonics(float note, float spread, float fine, float detune) {

      float fundamental = SemitonesToRatio(45.0f + fine - 69.0f) * a3;

      int k = static_cast<int>(note);

      float j=1;
      for (int i=kNumVoices-1; i>=0; i--, j+=spread) {

        int h = static_cast<int>(k + j);

        if (i&1 || !freeze_) {
          if (h >= 0) {
            phase_increment_[i] = fundamental * h * sqrtf(1.0f + detune * j * j);
          } else {
            phase_increment_[i] = -fundamental / (h - 2) * sqrtf(1.0f + detune * j * j);
          }
        }
      }
    }

    void set_structure(float structure) {
      for (int i=0; i<kNumVoices; i++) {
        modulation_matrix_[i] = InterpolateSine(modulation_table[i], structure, kNumStructures - 1);
      }
    }

    void set_self_feedback(float feedback) {
      self_feedback_ = feedback;
    }

    void set_modulation_index(float index) {
      modulation_index_ = index;
    }

    void set_freeze(float freeze) {
      freeze_ = freeze;
    }

    void set_bitcrush(float bitcrush) {
      bitcrush_ = bitcrush;
    }

    void set_softclip(float softclip) {
      softclip_ = softclip;
    }

    void set_decimate(float decimate) {
      decimate_ = decimate;
    }

  private:

    /* returns the closest value to x in the given sorted array */
    float binary_search(float x, const float array[], int size) {
      int low = 0;
      int high = size-1;

      while (low < high) {
        int mid = (low + high) / 2;
        if (fabs(array[mid] - x) < fabs(array[mid+1] - x)) {
          high = mid;
        } else {
          low = mid + 1;
        }
      }
      return array[high];
    }

    /* returns the closest rational to x with denominator smaller than
     * n */

    float closest_rational(float x, int n) {
      int i=0;

      while (x > 2) {x /= 2.0f; i++; }
      x = closrat(x - 1.0f, n) + 1.0f;
      while (i--) { x *= 2.0f; }

      return x;
    }

    float closrat(float x, int n) {
      float a, b, c, d;
      a = 0;
      b = c = d = 1;

      while (b <= n && d <= n) {
        float med = (a+c)/(b+d);
        if (x == med) {
          if (b+d <= n) return med;
          else if (d > b) return c/d;
          else return a/b;
        } else if (x > med) {
          a += c;
          b += d;
        } else {
          c += a;
          d += b;
        }
      }

      if (b > n) return c/d;
      else return a/b;
    }

    inline float InterpolateSine(const float* table, float index, float size) {
      index *= size;
      MAKE_INTEGRAL_FRACTIONAL(index)
        float a = table[index_integral];
      float b = table[index_integral + 1];
      index_fractional = Interpolate(lut_raised_cos, index_fractional, 256.0f);
      return a + (b - a) * index_fractional;
    }

    /* parameters */
    bool freeze_;
    float self_feedback_;
    float bitcrush_;
    float decimate_;
    float softclip_;
    float modulation_index_;

    float phase_[kNumVoices];
    float phase_increment_[kNumVoices];
    float self_feedback_sample_[kNumVoices][2];
    float modulation_sample_[kNumVoices];
    float modulation_matrix_[kNumVoices];
  };
}

#endif
