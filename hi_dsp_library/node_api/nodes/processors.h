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
 *   which also must be licenced for commercial applications:
 *
 *   http://www.juce.com
 *
 *   ===========================================================================
 */

#pragma once

namespace scriptnode
{
using namespace juce;
using namespace hise;

using namespace snex;
using namespace snex::Types;



namespace scriptnode_initialisers
{
struct oversample;
}

using namespace snex::Types;

/** The wrap namespace contains templates that wrap around an existing node type and change the processing.

	Examples are:

	- fixed channel amount: wrap::fix
	- fixed upper block size: wrap::fix_block
	- frame based processing: wrap::frame_x

	Most of the templates just hold an object of the given type and forward most callbacks to its inner object
	while adding the additional functionality where required.
*/
namespace wrap
{



/** This namespace holds all the logic of custom node wrappers in static functions so it can be used
	by the SNEX jit compiler as well as the C++ classes.

*/
namespace static_functions
{



template <int ChannelAmount> struct frame
{
	static void prepare(void* obj, prototypes::prepare f, const PrepareSpecs& ps)
	{
		auto ps_ = ps.withNumChannelsT<ChannelAmount>().withBlockSize(1);
		f(obj, &ps_);
	}
};

template <int BlockSize> struct fix_block
{
	HISE_EMPTY_INITIALISE;

	static void prepare(void* obj, prototypes::prepare f, const PrepareSpecs& ps)
	{
		auto ps_ = ps.withBlockSizeT<BlockSize>();
		f(obj, &ps_);
	}

	template <typename ProcessDataType> static void process(void* obj, prototypes::process<ProcessDataType> pf, ProcessDataType& data)
	{
		int numToDo = data.getNumSamples();

		if (numToDo < BlockSize)
			pf(obj, &data);
		else
		{
			// We need to forward the HiseEvents to the chunks as there might
			// be a event node sitting in there...
			static constexpr bool IncludeHiseEvents = true;

			ChunkableProcessData<ProcessDataType, IncludeHiseEvents> cpd(data);

			while (cpd)
			{
				int numThisTime = jmin(BlockSize, cpd.getNumLeft());
				auto c = cpd.getChunk(numThisTime);
				pf(obj, &c.toData());
			}
		}
	}
};



/** Continue... */
struct event
{
	template <typename ProcessDataType> static void process(void* obj, prototypes::process<ProcessDataType> pf, prototypes::handleHiseEvent ef, ProcessDataType& d)
	{
		auto events = d.toEventData();

		if (events.size() > 0)
		{
			// We don't need the event in child nodes as it should call 
			// handleHiseEvent recursively from here...
			static constexpr bool IncludeHiseEvents = false;

			ChunkableProcessData<ProcessDataType, IncludeHiseEvents> aca(d);

			int lastPos = 0;

			for (auto& e : events)
			{
				if (e.isIgnored())
					continue;

				auto samplePos = e.getTimeStamp();

				const int numThisTime = jmin(aca.getNumLeft(), samplePos - lastPos);

				if (numThisTime > 0)
				{
					auto c = aca.getChunk(numThisTime);
					pf(obj, &c.toData());
				}

				ef(obj, &e);
				lastPos = samplePos;
			}

			if (aca)
			{
				auto c = aca.getRemainder();
				pf(obj, &c.toData());
			}
		}
		else
		{
			pf(obj, &d);
		}
	}
};

}



/** This wrapper template will create a compile-time channel configuration which might
	increase the performance and inlineability.

	Usage:

	@code
	using stereo_oscillator = fix<2, core::oscillator>
	@endcode

	If you wrap a node into this template, it will call the process functions with
	the fixed size process data arguments:

	void process(ProcessData<C>& data)

	void processFrame(span<float, C>& data)

	instead of the dynamic ones.
*/
template <int C, class T> class fix
{
public:

	SN_OPAQUE_WRAPPER(fix, T);

	static const int NumChannels = C;

	/** The process data type to use for this node. */
	using FixProcessType = snex::Types::ProcessData<NumChannels>;

	/** The frame data type to use for this node. */
	using FixFrameType = snex::Types::span<float, NumChannels>;

	constexpr OPTIONAL_BOOL_CLASS_FUNCTION(isPolyphonic);

	OPTIONAL_BOOL_CLASS_FUNCTION(isProcessingHiseEvent);

	static Identifier getStaticId() { return T::getStaticId(); }

	/** Forwards the callback to its wrapped object. */
	void initialise(NodeBase* n)
	{
		this->obj.initialise(n);
	}

	/** Forwards the callback to its wrapped object, but it will change the channel amount to NumChannels. */
	void prepare(PrepareSpecs ps)
	{
		ps.numChannels = NumChannels;
		this->obj.prepare(ps);
	}

	/** Forwards the callback to its wrapped object. */
	forcedinline void reset() noexcept { obj.reset(); }

	/** Forwards the callback to its wrapped object. */
	void handleHiseEvent(HiseEvent& e)
	{
		this->obj.handleHiseEvent(e);
	}

	/** Processes the given data, but will cast it to the fixed channel version.

		You must not call this function with a ProcessDataType that has less channels
		or the results will be unpredictable.

		Leftover channels will not be processed.
	*/
	template <typename ProcessDataType> void process(ProcessDataType& data) noexcept
	{
		jassert(data.getNumChannels() >= NumChannels);
        auto& fd = data.template as<FixProcessType>();
		this->obj.process(fd);
	}


	/** Processes the given data, but will cast it to the fixed channel version.

		You must not call this function with a FrameDataType that has less channels
		or the results will be unpredictable.

		Leftover channels will not be processed.
	*/
	template <typename FrameDataType> forcedinline void processFrame(FrameDataType& data) noexcept
	{
		jassert(data.size() >= NumChannels);
		auto& d = FixFrameType::as(data.begin());
		this->obj.processFrame(d);
	}

	/** Forwards the callback to its wrapped object. */
	void createParameters(ParameterDataList& data)
	{
		this->obj.createParameters(data);
	}

private:

	T obj;
};


/** A wrapper around a node that allows custom initialisation.

	Just create an initialiser class that takes a reference
	to a T object and initialises it and it will do so in 
	the constructor of this class.
	
	You can use it in order to setup default values and
	add parameter connections for container nodes.
*/
template <class T, class Initialiser> class init
{
public:

	SN_SELF_AWARE_WRAPPER(init, T);

	init() : obj(), i(obj) {};

	HISE_DEFAULT_PREPARE(T);
	HISE_DEFAULT_RESET(T);
	HISE_DEFAULT_HANDLE_EVENT(T);
	HISE_DEFAULT_PROCESS_FRAME(T);
	HISE_DEFAULT_PROCESS(T);
	HISE_DEFAULT_MOD(T);

	void initialise(NodeBase* n)
	{
		if constexpr(prototypes::check::initialise<T>::value)
			obj.initialise(n);

		i.initialise(n);
	}

	constexpr OPTIONAL_BOOL_CLASS_FUNCTION(isPolyphonic);
	constexpr OPTIONAL_BOOL_CLASS_FUNCTION(isNormalisedModulation);
	OPTIONAL_BOOL_CLASS_FUNCTION(isProcessingHiseEvent);
	
	void createParameters(ParameterDataList& list)
	{
		if constexpr (prototypes::check::createParameters<T>::value)
			obj.createParameters(list);
	}

	static Identifier getStaticId() { return T::getStaticId(); };

	template <int P> static void setParameterStatic(void* obj, double value)
	{
		static_cast<init*>(obj)->setParameter<P>(value);
	}

	template <int P> void setParameter(double value)
	{
		obj.template setParameter<P>(value);
	}

	T obj;
	Initialiser i;
};


/** Wrapping a node into this template will bypass all callbacks.

	You can use it to quickly deactivate a node (or the code generator might use this
	in order to generate nodes that are bypassed.

*/
template <class T> class skip
{
public:

	SN_OPAQUE_WRAPPER(skip, T);

	void initialise(NodeBase* n)
	{
		this->obj.initialise(n);
	}

	void prepare(PrepareSpecs) {}
	void reset() noexcept {}

	template <typename ProcessDataType> void process(ProcessDataType&) noexcept {}

	void handleHiseEvent(HiseEvent&) {}

	template <typename FrameDataType> void processFrame(FrameDataType&) noexcept {}

private:

	T obj;
};

template <class T> class event
{
public:

	SN_OPAQUE_WRAPPER(event, T);

	constexpr OPTIONAL_BOOL_CLASS_FUNCTION(isPolyphonic);
	OPTIONAL_BOOL_CLASS_FUNCTION(isProcessingHiseEvent);

	HISE_DEFAULT_INIT(T);
	HISE_DEFAULT_PREPARE(T);
	HISE_DEFAULT_RESET(T);
	HISE_DEFAULT_HANDLE_EVENT(T);
	HISE_DEFAULT_PROCESS_FRAME(T);

	template <typename ProcessDataType> void process(ProcessDataType& data)
	{
        auto p = prototypes::static_wrappers<T>::template process<ProcessDataType>;
		auto e = prototypes::static_wrappers<T>::handleHiseEvent;
		static_functions::event::process<ProcessDataType>(this, p, e, data);
	}

	T obj;

private:
};

template <class T> class no_data
{
public:

	SN_SELF_AWARE_WRAPPER(no_data, T);

	constexpr OPTIONAL_BOOL_CLASS_FUNCTION(isPolyphonic);
	OPTIONAL_BOOL_CLASS_FUNCTION(isProcessingHiseEvent);

	HISE_DEFAULT_INIT(T);
	HISE_DEFAULT_PREPARE(T);
	HISE_DEFAULT_RESET(T);
	HISE_DEFAULT_MOD(T);
	HISE_DEFAULT_HANDLE_EVENT(T);
	HISE_DEFAULT_PROCESS_FRAME(T);
	HISE_DEFAULT_PROCESS(T);

	constexpr auto& getParameter() { return obj.getParameter(); };

	void setExternalData(const ExternalData& /*d*/, int /*index*/)
	{

	}

	

	template <int P> void setParameter(double v)
	{
		this->obj.template setParameter<P>(v);
	}
	FORWARD_PARAMETER_TO_MEMBER(no_data)

	T obj;
};


template <class T> class frame_x
{
public:

	SN_OPAQUE_WRAPPER(frame_x, T);

	void initialise(NodeBase* n)
	{
		obj.initialise(n);
	}

	void prepare(PrepareSpecs ps)
	{
		ps.blockSize = 1;
		obj.prepare(ps);
	}

	void handleHiseEvent(HiseEvent& e)
	{
		obj.handleHiseEvent(e);
	}

	forcedinline void reset() noexcept { obj.reset(); }

	template <typename ProcessDataType> void process(ProcessDataType& data)
	{
		constexpr int C = ProcessDataType::getNumFixedChannels();

		if constexpr (ProcessDataType::hasCompileTimeSize())
			FrameConverters::processFix<C>(&obj, data);
		else
			FrameConverters::forwardToFrame16(&obj, data);
	}

	template <typename FrameDataType> void processFrame(FrameDataType& data) noexcept
	{
		this->obj.processFrame(data);
	}

private:

	T obj;
};

/** Just a shortcut to the frame_x type. */
template <int C, typename T> using frame = fix<C, frame_x<T>>;

#if 0
template <int NumChannels, class T> class frame
{
public:

	GET_SELF_OBJECT(obj);
	GET_WRAPPED_OBJECT(obj);

	using FixProcessType = snex::Types::ProcessData<NumChannels>;
	using FrameType = snex::Types::span<float, NumChannels>;

	void initialise(NodeBase* n)
	{
		obj.initialise(n);
	}

	void prepare(PrepareSpecs ps)
	{
		ps.numChannels = NumChannels;
		obj.prepare(ps);
	}

	void handleHiseEvent(HiseEvent& e)
	{
		obj.handleHiseEvent(e);
	}

	forcedinline void reset() noexcept { obj.reset(); }

	template <typename ProcessDataType> void process(ProcessDataType& data)
	{
		snex::Types::ProcessDataHelpers<NumChannels>::processFix(&obj, data);
	}

	template <typename FrameDataType> void processFrame(FrameDataType& data) noexcept
	{
		this->obj.processFrame(data);
	}

private:

	T obj;
};
#endif



struct oversample_base
{
	using Oversampler = juce::dsp::Oversampling<float>;

	oversample_base(int factor) :
		oversamplingFactor(factor)
	{};

	void prepare(PrepareSpecs ps)
	{
		ScopedPointer<Oversampler> newOverSampler;

		auto originalBlockSize = ps.blockSize;

		ps.sampleRate *= (double)oversamplingFactor;
		ps.blockSize *= oversamplingFactor;

		if (prepareFunc)
			prepareFunc(pObj, &ps);
		
		newOverSampler = new Oversampler(ps.numChannels, (int)std::log2(oversamplingFactor), Oversampler::FilterType::filterHalfBandPolyphaseIIR, false);

		if (originalBlockSize > 0)
			newOverSampler->initProcessing(originalBlockSize);

		{
			hise::SimpleReadWriteLock::ScopedReadLock sl(lock);
			oversampler.swapWith(newOverSampler);
		}
	}

protected:

	hise::SimpleReadWriteLock lock;

	const int oversamplingFactor = 0;
	
	void* pObj = nullptr;
	prototypes::prepare prepareFunc;

	ScopedPointer<Oversampler> oversampler;
};





template <int OversamplingFactor, class T, class InitFunctionClass=scriptnode_initialisers::oversample> class oversample: public oversample_base
{
public:

	SN_OPAQUE_WRAPPER(oversample, T);

	oversample():
		oversample_base(OversamplingFactor)
	{
		this->prepareFunc = prototypes::static_wrappers<T>::prepare;
		this->pObj = &obj;
	}

	forcedinline void reset() noexcept 
	{
		hise::SimpleReadWriteLock::ScopedTryReadLock sl(this->lock);

		if (sl)
		{
			if(oversampler != nullptr)
				oversampler->reset();
		}

		obj.reset(); 
	}

	void handleHiseEvent(HiseEvent& e)
	{
		obj.handleHiseEvent(e);
	}

	template <typename FrameDataType> void processFrame(FrameDataType& data) noexcept
	{
		// shouldn't be called since the oversampling always has to happen on block level...
		jassertfalse;
	}

	template <typename ProcessDataType>  void process(ProcessDataType& data)
	{
		hise::SimpleReadWriteLock::ScopedTryReadLock sl(this->lock);

		if (sl)
		{
			if (oversampler == nullptr)
				return;

			auto bl = data.toAudioBlock();
			auto output = oversampler->processSamplesUp(bl);

			float* tmp[NUM_MAX_CHANNELS];

			for (int i = 0; i < data.getNumChannels(); i++)
				tmp[i] = output.getChannelPointer(i);

			ProcessDataType od(tmp, data.getNumSamples() * OversamplingFactor, data.getNumChannels());

			od.copyNonAudioDataFrom(data);
			obj.process(od);

			oversampler->processSamplesDown(bl);
		}
	}

	void initialise(NodeBase* n)
	{
		obj.initialise(n);
	}

private:

	T obj;
};


template <class T> class default_data
{
	static const int NumTables = T::NumTables;
	static const int NumSliderPacks = T::NumSliderPacks;
	static const int NumAudioFiles = T::NumAudioFiles;
	static const int NumFilters = T::NumFilters;
	static const int NumDisplayBuffers = T::NumDisplayBuffers;

	default_data(T& obj)
	{
		block b;
		obj.setExternalData(b, -1);
	}

	void setExternalData(T& obj, const snex::ExternalData& data, int index)
	{
		obj.setExternalData(data, index);
	}
};



/** A wrapper that extends the wrap::init class with the possibility of handling external data.

	The DataHandler class needs to have a constructor with a T& argument (where you can do the 
	usual parameter initialisations). On top of that you need to:
	
	1. Define 3 static const int values: `NumTables`, `NumSliderPacks` and `NumAudioFiles`
	2. Define a `void setExternalData(T& obj, int index, const block& b)` method that distributes the
	   incoming blocks to its children.


	*/
template <class T, class DataHandler = default_data<T>> struct data : public wrap::init<T, DataHandler>,
																	  public scriptnode::data::pimpl::provider_base
{
	SN_SELF_AWARE_WRAPPER(data, T);

	static constexpr size_t getDataOffset()
	{
		using D = data<T, DataHandler>;
		return offsetof(D, i);
	}

	data()
	{
		static_assert(std::is_base_of<scriptnode::data::pimpl::base, DataHandler>(), "DataHandler must be base class of data::pimpl::base");
		static_assert(std::is_base_of<scriptnode::data::base, typename T::WrappedObjectType>() ||
					  std::is_base_of<scriptnode::data::base, typename T::ObjectType>(), "T must be base class of data::base");

		if constexpr (mothernode::isBaseOf<T>())
			mothernode::getAsBase(getObject())->setDataProvider(this);
	}

	static const int NumTables = DataHandler::NumTables;
	static const int NumSliderPacks = DataHandler::NumSliderPacks;
	static const int NumAudioFiles = DataHandler::NumAudioFiles;
	static const int NumFilters = DataHandler::NumFilters;
	static const int NumDisplayBuffers = DataHandler::NumDisplayBuffers;

	scriptnode::data::pimpl::base* getDataObject() override
	{
		return &this->i;
	}

	auto& getParameter() { return this->obj.getParameter(); }

	void setExternalData(const snex::ExternalData& data, int index)
	{
		this->i.setExternalData(this->obj, data, index);
	}

	template <int P> static void setParameterStatic(void* obj, double value)
	{
		static_cast<data*>(obj)->setParameter<P>(value);
	}

	template <int P> void setParameter(double v)
	{
		T::setParameterStatic<P>(&this->obj, v);
	}

	JUCE_DECLARE_WEAK_REFERENCEABLE(data);
};



/** A wrapper node that will render its child node to a external data object. */
template <class T> class offline : public scriptnode::data::base
{
public:

	static const int NumTables = 0;
	static const int NumSliderPacks = 0;
	static const int NumAudioFiles = 1;
	static const int NumFilters = 0;
	static const int NumDisplayBuffers = 0;

	SN_SELF_AWARE_WRAPPER(offline, T);

	HISE_EMPTY_PROCESS;
	HISE_EMPTY_PROCESS_SINGLE;
	HISE_EMPTY_PREPARE;
	HISE_EMPTY_RESET;
	HISE_EMPTY_HANDLE_EVENT;
	
	void initialise(NodeBase* n) { obj.initialise(n); }

	void setExternalData(const snex::ExternalData& data, int index) override
	{
		if (recursion || data.isEmpty())
			return;

		ScopedValueSetter<bool> svs(recursion, true);

		PrepareSpecs ps;
		ps.blockSize = 512;
		ps.numChannels = data.numChannels;
		ps.sampleRate = data.sampleRate;

		getWrappedObject().prepare(ps);
		getWrappedObject().reset();
		
		ProcessDataDyn pd((float**)data.data, data.numSamples, data.numChannels);

		ChunkableProcessData cd(pd);

		while (cd.getNumLeft() > ps.blockSize)
		{
			auto c = cd.getChunk(ps.blockSize);
			getWrappedObject().process(c.toData());
		}

		if (cd.getNumLeft() > 0)
		{
			auto c = cd.getRemainder();
			getWrappedObject().process(c.toData());
		}

		if (auto af = dynamic_cast<MultiChannelAudioBuffer*>(data.obj))
		{
			af->loadBuffer(data.toAudioSampleBuffer(), data.sampleRate);
		}
	}

private:

	T obj;
	bool recursion = false;
};

#if 0
template <int BlockSize, class T> class fix_block
{
public:

	SN_OPAQUE_WRAPPER(fix_block, T);

	fix_block() {};

	void prepare(PrepareSpecs ps)
	{
		static_functions::fix_block<BlockSize>::prepare(this, prototypes::static_wrappers<T>::prepare, ps);
	}

	template <typename ProcessDataType> void process(ProcessDataType& data)
	{
        static_functions::fix_block<BlockSize>::process(this, prototypes::static_wrappers<T>::template process<ProcessDataType>, data);
	}

	template <typename FrameDataType> void processFrame(FrameDataType& t)
	{
		// should never happen...
		jassertfalse;
	}

	HISE_DEFAULT_INIT(T);
	HISE_DEFAULT_RESET(T);
	HISE_DEFAULT_MOD(T);
	HISE_DEFAULT_HANDLE_EVENT(T);

private:

	T obj;
};
#endif

template <class T> struct no_midi
{
	SN_OPAQUE_WRAPPER(no_midi, T);

	HISE_DEFAULT_RESET(T);
	HISE_DEFAULT_MOD(T);
	HISE_DEFAULT_INIT(T);
	HISE_DEFAULT_PROCESS(T);
	HISE_DEFAULT_PROCESS_FRAME(T);
	HISE_DEFAULT_PREPARE(T);
	HISE_EMPTY_HANDLE_EVENT;

	T obj;
};


template <class T, typename FixBlockClass> struct fix_blockx
{
	SN_OPAQUE_WRAPPER(fix_blockx, T);

	
	HISE_DEFAULT_RESET(T);
	HISE_DEFAULT_MOD(T);
	HISE_DEFAULT_HANDLE_EVENT(T);

	void initialise(NodeBase* n)
	{
		fbClass.initialise(n);
		obj.initialise(n);
	}

	void prepare(PrepareSpecs ps)
	{
		fbClass.prepare(this, prototypes::static_wrappers<T>::prepare, ps);
	}

	template <typename ProcessDataType> void process(ProcessDataType& data)
	{
		fbClass.process(this, prototypes::static_wrappers<T>::template process<ProcessDataType>, data);
	}

	template <typename FrameDataType> void processFrame(FrameDataType& t)
	{
		// should never happen...
		jassertfalse;
	}

	T obj;
	FixBlockClass fbClass;
};

template <int BlockSize, class T> struct fix_block: public fix_blockx<T, static_functions::fix_block<BlockSize>>
{
};



/** Downsamples the incoming signal with the HISE_EVENT_RASTER value
    (default is 8x) and processes a mono signal that can be used as
    modulation signal.
*/
template <class T> class control_rate
{
public:

	SN_OPAQUE_WRAPPER(control_rate, T);

	using FrameType = snex::Types::span<float, 1>;
	using ProcessType = snex::Types::ProcessData<1>;

	constexpr static bool isModulationSource = false;

	void initialise(NodeBase* n)
	{
		obj.initialise(n);
	}

	void prepare(PrepareSpecs ps)
	{
		bool isFrame = ps.blockSize == 1;

		if (!isFrame)
		{
			ps.sampleRate /= (double)HISE_EVENT_RASTER;
			ps.blockSize /= HISE_EVENT_RASTER;
		}
		
		ps.numChannels = 1;

		if(!isFrame)
			snex::Types::FrameConverters::increaseBuffer(controlBuffer, ps);

		this->obj.prepare(ps);
	}

	void reset()
	{
		obj.reset();
		singleCounter = 0;
	}

	template <typename ProcessDataType> void process(ProcessDataType& data)
	{
		int numToProcess = data.getNumSamples() / HISE_EVENT_RASTER;

		jassert(numToProcess <= controlBuffer.size());

		FloatVectorOperations::clear(controlBuffer.begin(), numToProcess);
		
		float* d[1] = { controlBuffer.begin() };

		ProcessType md(d, numToProcess, 1);
		md.copyNonAudioDataFrom(data);

		obj.process(md);
	}

	// must always be wrapped into a fix<1> node...
	template <typename FrameDataType> void processFrame(FrameDataType& )
	{
		FrameType md = { 0.0f };
		obj.processFrame(md);
	}

	bool handleModulation(double& )
	{
		return false;
	}

	void handleHiseEvent(HiseEvent& e)
	{
		obj.handleHiseEvent(e);
	}

	T obj;
	int singleCounter = 0;

	snex::Types::heap<float> controlBuffer;
};


/** The wrap::mod class can be used in order to create modulation connections between nodes.

	It uses the same parameter class as the containers in order to optimise as much as possible on 
	compile time.

	If you want a node to act as modulation source, you need to add a function with the prototype

	bool handleModulation(double& value)

	which sets the `value` parameter to the modulation value and returns true if the modulation is
	supposed to be sent out to the targets. The return parameter allows you to skip redundant modulation
	calls (eg. a tempo sync mod only needs to send out a value if the tempo has changed).

	The mod node will call this function on different occasions and if it returns true, forwards the value
	to the parameter class:

	- after a reset() call
	- after each process call. Be aware that processFrame will also cause a call to handle modulation so be 
	  aware of the performance implications here.
	- after each handleHiseEvent() callback. This can be used to make modulation sources that react on MIDI.

	A useful helper class for this wrapper is the ModValue class, which offers a few convenient functions.
*/
template <class ParameterClass, class T> struct mod
{
	SN_SELF_AWARE_WRAPPER(mod, T);

	template <typename ProcessDataType> void process(ProcessDataType& data) noexcept
	{
		obj.process(data);
		checkModValue();
	}

	/** Resets the node and sends a modulation signal if required. */
	void reset() noexcept 
	{
		obj.reset();
		checkModValue();
	}


	template <typename FrameDataType> void processFrame(FrameDataType& data) noexcept
	{
		obj.processFrame(data);
		checkModValue();
	}

	inline void initialise(NodeBase* n)
	{
		obj.initialise(n);
	}

	/** Calls handleHiseEvent on the wrapped object and sends out a modulation signal if required. */
	void handleHiseEvent(HiseEvent& e)
	{
		obj.handleHiseEvent(e);
		checkModValue();
	}

	constexpr OPTIONAL_BOOL_CLASS_FUNCTION(isPolyphonic);
	constexpr OPTIONAL_BOOL_CLASS_FUNCTION(isProcessingHiseEvent);

	void prepare(PrepareSpecs ps)
	{
		obj.prepare(ps);
	}

	/** This function will be called when a mod value is supposed to be 
	    sent out to the target. 
	*/
	bool handleModulation(double& value) noexcept
	{
		return obj.handleModulation(value);
	}

	/** This method can be used to connect a target to the parameter of this
	    modulation node. 
	*/
	template <int I, class TargetType> void connect(TargetType& t)
	{
		p.template getParameter<0>().template connect<I>(t);
	}

	ParameterClass& getParameter() { return p; }

	/** Forwards the setParameter to the wrapped node. (Required because this
	    node needs to be *SelfAware*. 
	*/
	template <int P> void setParameter(double v)
	{
		obj.template setParameter<P>(v);
	}

	template <int P> static void setParameterStatic(void* o, double v)
	{
		static_cast<mod*>(o)->template setParameter<P>(v);
	}

	void setExternalData(const ExternalData& d, int index)
	{
		if constexpr (prototypes::check::setExternalData<T>::value)
			obj.setExternalData(d, index);
	}

	T obj;
	ParameterClass p;

private:

	void checkModValue()
	{
		double modValue = 0.0;

		if (handleModulation(modValue))
			p.call(modValue);
	}
};


template <typename T> struct illegal_poly
{
	SN_GET_SELF_AS_OBJECT(illegal_poly);

	static Identifier getStaticId() { return T::getStaticId(); }

	static constexpr bool isPolyphonic() { return true; }

	void prepare(PrepareSpecs ps)
	{
		scriptnode::Error e;
		e.error = Error::IllegalPolyphony;
		throw e;
	}

	SN_DESCRIPTION("(not available in a poly network)");

	HISE_EMPTY_PROCESS;
	HISE_EMPTY_PROCESS_SINGLE;
	HISE_EMPTY_RESET;
	HISE_EMPTY_HANDLE_EVENT;
	HISE_EMPTY_MOD;
	
	void initialise(NodeBase* n) { obj.initialise(n); }

	void createParameters(ParameterDataList& l) { obj.createParameters(l); }
	T obj;
};



/** A "base class for the node template". */
struct DummyMetadata
{
	SNEX_METADATA_ID("NodeId");
	SNEX_METADATA_NUM_CHANNELS(2);
	SNEX_METADATA_PARAMETERS(3, "Value", "Reverb", "Funky");
};


template <class T> struct node : public scriptnode::data::base
{
	using MetadataClass = typename T::metadata;
	static constexpr bool isModulationSource = T::isModulationSource;
	static constexpr bool isNormalisedModulation() { return true; };

	static constexpr int NumChannels =	  MetadataClass::NumChannels;
	static constexpr int NumTables =	  MetadataClass::NumTables;
	static constexpr int NumSliderPacks = MetadataClass::NumSliderPacks;
	static constexpr int NumAudioFiles =  MetadataClass::NumAudioFiles;
	static constexpr int NumFilters =  MetadataClass::NumFilters;
	static constexpr int NumDisplayBuffers = MetadataClass::NumDisplayBuffers;

	// We treat everything in this node as opaque...
	SN_GET_SELF_AS_OBJECT(node);

	static bool constexpr isModNode() { return prototypes::check::handleModulation<T>::value; }

	static Identifier getStaticId() { return MetadataClass::getStaticId(); };

	using FixBlockType = snex::Types::ProcessData<NumChannels>;
	using FrameType = snex::Types::span<float, NumChannels>;

	node() :
		obj()
	{
	}

	void initialise(NodeBase* n)
	{
		obj.initialise(n);
	}

	template <int P> static void setParameterStatic(void* ptr, double v)
	{
		auto* objPtr = &static_cast<node*>(ptr)->obj;
		T::setParameter<P>(objPtr, v);
	}

	PARAMETER_MEMBER_FUNCTION;

	void process(FixBlockType& d)
	{
		obj.process(d);
	}

	void process(ProcessDataDyn& data) noexcept
	{
		jassert(data.getNumChannels() == NumChannels);
		auto& fd = data.as<FixBlockType>();
		obj.process(fd);
	}

	template <typename FrameDataType> void processFrame(FrameDataType& data) noexcept
	{
		auto& fd = FrameType::as(data.begin());
		obj.processFrame(fd);
	}

	void prepare(PrepareSpecs ps)
	{
		obj.prepare(ps);
	}

	void handleHiseEvent(HiseEvent& e)
	{
		obj.handleHiseEvent(e);
	}

	bool isPolyphonic() const
	{
		if constexpr (prototypes::check::isPolyphonic<T>::value)
			return obj.isPolyphonic();

		return false;
	}

	static constexpr bool isProcessingHiseEvent()
	{
		if constexpr (prototypes::check::isProcessingHiseEvent<T>::value)
			return T::isProcessingHiseEvent();

		return false;
	}

	void reset() noexcept { obj.reset(); }

	bool handleModulation(double& value) noexcept
	{
		if constexpr(prototypes::check::handleModulation<T>::value)
			return obj.handleModulation(value);

		return false;
	}

	void setExternalData(const ExternalData& d, int index) override
	{
		if constexpr (prototypes::check::setExternalData<T>::value)
			obj.setExternalData(d, index);
	}

	void createParameters(ParameterDataList& data)
	{
		ParameterDataList l;
		obj.parameters.addToList(l);

		auto peList = parameter::encoder::fromNode<node>();

		for (const parameter::pod& p : peList)
		{
			if (isPositiveAndBelow(p.index, l.size()))
			{
				auto& r = l.getReference(p.index);
				r.info = p;
			}
		}

		data.addArray(l);
	}



	T obj;
};




using namespace snex;
using namespace Types;

template <typename T, int NumDuplicates=16>	  using duplichain = duplicate_base<T, options::no, options::yes, NumDuplicates>;
template <typename T, int NumDuplicates = 16> using duplisplit = duplicate_base<T, options::yes, options::yes, NumDuplicates>;
template <typename T, int NumDuplicates>	  using fix_duplichain = duplicate_base<T, options::no, options::no, NumDuplicates>;
template <typename T, int NumDuplicates>	  using fix_duplisplit = duplicate_base<T, options::yes, options::no, NumDuplicates>;

}





}