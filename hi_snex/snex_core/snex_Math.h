/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   Commercial licences for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licencing:
*
*   http://www.hartinstruments.net/hise/
*
*   HISE is based on the JUCE library,
*   which also must be licenced for commercial applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

#pragma once

namespace snex {


using block = Types::dyn<float>;


#if JUCE_LINUX
#define std_
#else
#define std_ std
#endif

struct hmath
{

	constexpr static double PI = 3.1415926535897932384626433832795;
	constexpr static double E = 2.7182818284590452353602874713527;
	constexpr static double SQRT2 = 1.4142135623730950488016887242097;
	constexpr static double FORTYTWO = 42.0; // just for unit test purposes, the other ones choke because of 
									   // String conversion imprecisions...

	static forcedinline double sign(double value) { return value > 0.0 ? 1.0 : -1.0; };
	static forcedinline double abs(double value) { return value * sign(value); };
	static forcedinline double round(double value) { return roundf((float)value); };
	static forcedinline double range(double value, double lower, double upper) { return jlimit<double>(lower, upper, value); };
	static forcedinline double min(double value1, double value2) { return jmin<double>(value1, value2); };
	static forcedinline double max(double value1, double value2) { return jmax<double>(value1, value2); };
	static forcedinline double randomDouble() { return Random::getSystemRandom().nextDouble(); };

	static forcedinline float sign(float value) { return value > 0.0f ? 1.0f : -1.0f; };
	static forcedinline float abs(float value) { return value * sign(value); };
	static forcedinline float round(float value) { return roundf((float)value); };
	static forcedinline float range(float value, float lower, float upper) { return jlimit<float>(lower, upper, value); };
	static forcedinline float min(float value1, float value2) { return jmin<float>(value1, value2); };
	static forcedinline float max(float value1, float value2) { return jmax<float>(value1, value2); };
	static forcedinline float random() { return Random::getSystemRandom().nextFloat(); };
	static forcedinline float fmod(float x, float y) { return std::fmod(x, y); };

	template <class SpanT> static auto sumSpan(const SpanT& t) { return t.accumulate(); };



	static forcedinline int min(int value1, int value2) { return jmin<int>(value1, value2); };
	static forcedinline int max(int value1, int value2) { return jmax<int>(value1, value2); };
	static forcedinline int range(int value, int lower, int upper)
	{
		return jlimit<int>(lower, upper, value);
	};
	static int round(int value) { return value; };
	static forcedinline int randInt(int low = 0, int high = INT_MAX) { return  Random::getSystemRandom().nextInt(Range<int>((int)low, (int)high)); }

	static forcedinline double map(double normalisedInput, double start, double end) { return jmap<double>(normalisedInput, start, end); }
	static forcedinline double sin(double a) { return std_::sin(a); }
	static forcedinline double asin(double a) { return std_::asin(a); }
	static forcedinline double cos(double a) { return std_::cos(a); }
	static forcedinline double acos(double a) { return std_::acos(a); }
	static forcedinline double sinh(double a) { return std_::sinh(a); }
	static forcedinline double cosh(double a) { return std_::cosh(a); }
	static forcedinline double tan(double a) { return std_::tan(a); }
	static forcedinline double tanh(double a) { return std_::tanh(a); }
	static forcedinline double atan(double a) { return std_::atan(a); }
	static forcedinline double atanh(double a) { return std_::atanh(a); }
	static forcedinline double log(double a) { return std_::log(a); }
	static forcedinline double log10(double a) { return std_::log10(a); }
	static forcedinline double exp(double a) { return std_::exp(a); }
	static forcedinline double pow(double base, double exp) { return std_::pow(base, exp); }
	static forcedinline double sqr(double a) { return a * a; }
	static forcedinline double sqrt(double a) { return std_::sqrt(a); }
	static forcedinline double ceil(double a) { return std_::ceil(a); }
	static forcedinline double floor(double a) { return std_::floor(a); }
	static forcedinline double db2gain(double a) { return Decibels::decibelsToGain(a); }
	static forcedinline double gain2db(double a) { return Decibels::gainToDecibels(a); }
	static forcedinline double fmod(double x, double y)
	{
		return std::fmod(x, y);
	};

	static forcedinline float map(float normalisedInput, float start, float end) { return jmap<float>(normalisedInput, start, end); }
	static forcedinline float sin(float a) { return std_::sinf(a); }
	static forcedinline float asin(float a) { return std_::asinf(a); }
	static forcedinline float cos(float a) { return std_::cosf(a); }
	static forcedinline float acos(float a) { return std_::acosf(a); }
	static forcedinline float sinh(float a) { return std_::sinhf(a); }
	static forcedinline float cosh(float a) { return std_::coshf(a); }
	static forcedinline float tan(float a) { return std_::tanf(a); }
	static forcedinline float tanh(float a) { return std_::tanhf(a); }
	static forcedinline float atan(float a) { return std_::atanf(a); }
	static forcedinline float atanh(float a) { return std_::atanhf(a); }
	static forcedinline float log(float a) { return std_::logf(a); }
	static forcedinline float log10(float a) { return std_::log10f(a); }
	static forcedinline float exp(float a) { return std_::expf(a); }
	static forcedinline float pow(float base, float exp) { return std_::powf(base, exp); }
	static forcedinline float sqr(float a) { return a * a; }
	static forcedinline float sqrt(float a) { return std_::sqrtf(a); }
	static forcedinline float ceil(float a) { return std_::ceilf(a); }
	static forcedinline float floor(float a) { return std_::floorf(a); }
	static forcedinline float db2gain(float a) { return Decibels::decibelsToGain(a); }
	static forcedinline float gain2db(float a) { return Decibels::gainToDecibels(a); }

	struct wrapped
	{
		struct sin {
			static constexpr char name[] = "sin";
			static forcedinline float op(float a) { return hmath::sin(a); }
			static forcedinline double op(double a) { return hmath::sin(a); }
		};

		struct tanh {
			static constexpr char name[] = "tanh";
			static forcedinline float op(float a) { return hmath::tanh(a); }
			static forcedinline double op(double a) { return hmath::tanh(a); }
		};
	};


	static void throwIfSizeMismatch(const block& b1, const block& b2)
	{
		if (b1.size() != b2.size())
			throw String("Size mismatch");
	}

#define vOpBinary(name, vectorOp) static forcedinline block& name(block& b1, const block& b2) { \
	throwIfSizeMismatch(b1, b2); \
	vectorOp(b1.data, b2.data, b1.size()); \
	return b1; \
};

#define vOpScalar(name, vectorOp) static forcedinline block& name(block& b1, float s) { \
	vectorOp(b1.data, s, b1.size()); \
	return b1; \
};

	vOpBinary(vmul, FloatVectorOperations::multiply);
	vOpBinary(vadd, FloatVectorOperations::add);
	vOpBinary(vsub, FloatVectorOperations::subtract);
	vOpBinary(vcopy, FloatVectorOperations::copy);

	vOpScalar(vset, FloatVectorOperations::fill);
	vOpScalar(vmuls, FloatVectorOperations::multiply);
	vOpScalar(vadds, FloatVectorOperations::add);

#undef vOpBinary
#undef vOpScalar

};

}