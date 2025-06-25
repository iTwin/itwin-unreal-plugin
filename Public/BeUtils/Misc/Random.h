/*--------------------------------------------------------------------------------------+
|
|     $Source: Random.h $
|
|  $Copyright: (c) 2025 Bentley Systems, Incorporated. All rights reserved. $
|
+--------------------------------------------------------------------------------------*/


#pragma once

namespace BeUtils
{

// Imported from vue.git/Vue/Source/eonLib/Random.h
class RandomNumberGenerator
{
public:
	RandomNumberGenerator(uint32_t nSeed)
		: nSeed_(nSeed)
	{
	}

	~RandomNumberGenerator()
	{
	}

	void SetSeed(uint32_t nSeed)
	{
		nSeed_ = nSeed;
	}

	int Rand()
	{
		return RandInt();
	}

	int RandInt()
	{
		nSeed_ = nSeed_ * 0x15a4e35 + 1;
		return (nSeed_ >> 16) & 0x7FFF;
	}

	uint32_t RandUInt()
	{
		nSeed_ = nSeed_ * 0x15a4e35 + 1;
		return (nSeed_ >> 16) & 0x7FFF;
	}

	//! Return a pseudo-random floating number in range [0.0f, 1.0f]
	float RandFloat()
	{
		// Remark: 0.000030517578125f === 1.f/32768 === 1.f / 0x8000
		return 0.000030517578125f * RandInt();
	}

	//! Return a pseudo-random floating number in range [0.0f, fMax]
	float RandFloat(float fMax)
	{
		return fMax * RandFloat();
	}

	//! Return a pseudo-random floating number (double precision) in range [0.0, 1.0]
	double RandDouble()
	{
		// Remark: 0.000030517578125 === 1./32768 === 1. / 0x8000
		return 0.000030517578125 * RandInt();
	}

	//! Return a pseudo-random floating number (double precision) in range [0.0, dMax]
	double RandDouble(double dMax)
	{
		return dMax * RandDouble();
	}

	//! Return a pseudo-random integer in range [0, nMax]
	int RandInt(int nMax)
	{
		int res = int(nMax * RandFloat());
		return res;
	}

private:
	uint32_t nSeed_ = 0;
};

} // ns. BeUtils
