/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   Commercial licenses for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licensing:
*
*   http://www.hise.audio/
*
*   HISE is based on the JUCE library,
*   which must be separately licensed for closed source applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

namespace hise {
using namespace juce;

ONNXLoader::ONNXLoader(const String& rootDir):
	ok(Result::fail("dll could not open"))
{
	if(File::isAbsolutePath(rootDir) && File(rootDir).isDirectory())
	{
		auto dllFile = File(rootDir);

#if JUCE_WINDOWS
		dllFile = dllFile.getChildFile("onnx_hise_library.dll");
#elif JUCE_MAC
		dllFile = dllFile.getChildFile("onnx_hise_library.dylib");
#elif JUCE_LINUX
		// DAVID!!!!
		jassertfalse;
#endif

		if(!dllFile.existsAsFile())
		{
			ok = Result::fail("Missing dynamic library " + dllFile.getFullPathName());
		}
		else
		{
			dll = new DynamicLibrary();

			if(dll->open(dllFile.getFullPathName()))
				ok = Result::ok();
			else
				dll = nullptr;
		}
	}
}

ONNXLoader::~ONNXLoader()
{
	if(currentModel != nullptr)
	{
		jassert(dll != nullptr);

		if(auto f = getFunction<destroyModel_f>("destroyModel"))
			f(currentModel);

		currentModel = nullptr;
		dll = nullptr;
	}
}

bool ONNXLoader::run(const Image& img, std::vector<float>& outputValues, bool isGreyscale)
{
	if(currentModel != nullptr)
	{
		if(auto f = getFunction<run_f>("runModel"))
		{
			MemoryOutputStream mos;
			PNGImageFormat().writeImageToStream(img, mos);
			mos.flush();

			if(f(currentModel, mos.getData(), mos.getDataSize(), outputValues.size(), isGreyscale))
			{
				if(auto w = getFunction<getOutput_f>("getOutput"))
				{
					w(currentModel, outputValues.data());
				}
			}

			setLastError();
		}
	}

	return ok.wasOk();
}

Result ONNXLoader::loadModel(const MemoryBlock& mb)
{
	if(dll != nullptr)
	{
		if(auto f = getFunction<loadModel_f>("loadModel"))
		{
			currentModel = f(mb.getData(), mb.getSize());
			jassert(currentModel != nullptr);
			setLastError();
		}
	}
	else
	{
		jassert(!ok.wasOk());
	}

	return ok;
}

Result ONNXLoader::getLastError() const
{
	return ok;
}

void ONNXLoader::setLastError()
{
	if(auto f = getFunction<getError_f>("getError"))
	{
		char buffer[2048];
		size_t num = 0;

		if(!f(currentModel, buffer, num))
		{
			ok = Result::fail(String(buffer, num));
		}
		else
			ok = Result::ok();
	}
}

} // namespace hise