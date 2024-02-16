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




namespace hise
{
using namespace juce;

namespace multipage {
using namespace juce;

	
#if HISE_MULTIPAGE_INCLUDE_EDIT
struct State::StateProvider: public ApiProviderBase
{
	static DebugInformationBase::Ptr createRecursive(const String& name, const var& value);

	struct SettableObject: public SettableDebugInfo
	{
		virtual Ptr getChildElement(int index)
        {
			auto id = obj.getDynamicObject()->getProperties().getName(index);

			String s;
            s << name << '.' << id;

            return createRecursive(s, obj[id]);
        }

        int getNumChildElements() const override
        {
	        return obj.getDynamicObject()->getProperties().size();
        }

		var obj;
	};

    struct SettableArray: public SettableDebugInfo
    {
        virtual Ptr getChildElement(int index)
        {
            String s;
            s << name << '[' << String(index) << ']';
            return createRecursive(s, list[index]);
        }

        int getNumChildElements() const override
        {
	        return list.size();
        }

        var list;
    };

    StateProvider(State& parent_):
      parent(parent_)
    {}

        /** Override this method and return the number of all objects that you want to use. */
    int getNumDebugObjects() const override
    {
	    return parent.globalState.getDynamicObject()->getProperties().size();
    }

	DebugInformationBase::Ptr getDebugInformation(int index) override
    {
	    auto id = parent.globalState.getDynamicObject()->getProperties().getName(index);
        auto value = parent.globalState[id];
        return createRecursive(id.toString(), value);
    }

    State& parent;
};

DebugInformationBase::Ptr State::StateProvider::createRecursive(const String& name, const var& value)
{
	SettableDebugInfo* ni;

	if(value.isArray())
	{
		auto sa = new SettableArray();
		sa->list = value;
		ni = sa;
	}
	else if (auto no = value.getDynamicObject())
	{
		auto so = new SettableObject();
		so->obj = no;
		ni = so;
	}
	else
	{
		ni = new SettableDebugInfo();
	}

	ni->name = name;
	ni->autocompleteable = false;
	ni->value = value.toString();

	return ni;
}
#endif

State::State(const var& obj):
	Thread("Tasks"),
#if HISE_MULTIPAGE_INCLUDE_EDIT
	stateProvider(new StateProvider(*this)),
#endif
	currentError(Result::ok())
{
	if(auto gs = obj[mpid::GlobalState].getDynamicObject())
		globalState = var(gs->clone());
	else
		globalState = var(new DynamicObject());

	auto d = globalState.getDynamicObject();

	
}

State::~State()
{
	stopThread(1000);

	tempFiles.clear();
}

void State::run()
{
	navigateOnFinish = false;

	for(int i = 0; i < jobs.size(); i++)
	{
		currentJob = jobs[i];
		
		auto ok = jobs[i]->runJob();

		currentJob = nullptr;
		
		if(ok.failed())
		{
			navigateOnFinish = false;
			break;
		}
			
            
		totalProgress = (double)i / (double)jobs.size();
	}
        
	jobs.clear();
        
	MessageManager::callAsync(BIND_MEMBER_FUNCTION_0(State::onFinish));
}

ApiProviderBase* State::getProviderBase()
{
	return stateProvider.get();
}

State::Job::Job(State& rt, const var& obj):
	parent(rt),
	localObj(obj),
	callOnNextEnabled(obj[mpid::CallOnNext])
{
	
}

State::Job::~Job()
{}

void State::Job::postInit()
{
	if(!callOnNextEnabled)
		parent.addJob(this);
}

void State::Job::callOnNext()
{
	if(callOnNextEnabled)
	{
		parent.addJob(this);
		throw CallOnNextAction();
	}
}

bool State::Job::matches(const var& obj) const
{
	return localObj[mpid::ID] == obj[mpid::ID];
}

double& State::Job::getProgress()
{ return progress; }

void State::Job::setMessage(const String& newMessage)
{
	message = newMessage;

	if(parent.currentDialog != nullptr)
	{
		SafeAsyncCall::repaint(parent.currentDialog.get());
	}
}

void State::Job::updateProgressBar(ProgressBar* b) const
{
	if(message.isNotEmpty())
		b->setTextToDisplay(message);
}

State::Job::Ptr State::getJob(const var& obj)
{
	for(auto j: jobs)
	{
		if(j->matches(obj))
			return j;
	}
        
	return nullptr;
}

var State::getGlobalSubState(const Identifier& id)
{
	if(globalState.hasProperty(id))
		return globalState[id];

	var no = new DynamicObject();
	globalState.getDynamicObject()->setProperty(id, no);
	return no;
}

void State::onFinish()
{
	if(currentDialog.get() != nullptr)
	{
		currentDialog->nextButton.setEnabled(currentDialog->currentErrorElement == nullptr);
		currentDialog->prevButton.setEnabled(true);

		if(navigateOnFinish)
		{
			currentDialog->navigate(true);
		}
	}
}

Result State::Job::runJob()
{
	parent.navigateOnFinish |= callOnNextEnabled;

	auto ok = run();
            
	if(auto p = parent.currentDialog)
	{
		MessageManager::callAsync([p, ok]()
		{
			p->repaint();
			p->errorComponent.setError(ok);
		});
	}
            
	return ok;
}

void State::addJob(Job::Ptr b, bool addFirst)
{
	if(addFirst)
		jobs.insert(0, b);
	else
		jobs.add(b);
        
	if(!isThreadRunning())
	{
		if(currentDialog != nullptr)
		{
			currentDialog->currentErrorElement = nullptr;
			currentDialog->repaint();
			currentDialog->errorComponent.setError(Result::ok());
			currentDialog->nextButton.setEnabled(false);
			currentDialog->prevButton.setEnabled(false);
		}
            
		startThread(6);
	}
}

var CallableAction::operator()(const var::NativeFunctionArgs& args)
{
	globalState = args.thisObject;

	if(state.currentJob != nullptr)
		return perform(*state.currentJob);
        
	jassertfalse;
	return var();
}

var CallableAction::get(const Identifier& id) const
{
	return globalState[id];
}

LambdaAction::LambdaAction(State& s, const LambdaFunctionWithObject& of_):
	CallableAction(s),
	of(of_)
{}

LambdaAction::LambdaAction(State& s, const LambdaFunction& lf_):
	CallableAction(s),
	lf(lf_)
{}

var LambdaAction::perform(multipage::State::Job& t)
{
	if(lf)
		return lf(t);
	else if (of)
		return of(t, globalState);

	jassertfalse;
	return var();
}

UndoableVarAction::UndoableVarAction(const var& parent_, const Identifier& id, const var& newValue_):
	actionType(newValue_.isVoid() ? Type::RemoveProperty : Type::SetProperty),
	parent(parent_),
	key(id),
	index(-1),
	oldValue(parent[key]),
	newValue(newValue_)
{}

UndoableVarAction::UndoableVarAction(const var& parent_, int index_, const var& newValue_):
	actionType(newValue_.isVoid() ? Type::RemoveChild : Type::AddChild),
	parent(parent_),
	index(index_),
	oldValue(isPositiveAndBelow(index, parent.size()) ? parent[index] : var()),
	newValue(newValue_)
{}

bool UndoableVarAction::perform()
{
	switch(actionType)
	{
	case Type::SetProperty: parent.getDynamicObject()->setProperty(key, newValue); return true;
	case Type::RemoveProperty: parent.getDynamicObject()->removeProperty(key); true;
	case Type::AddChild: parent.getArray()->insert(index, newValue); return true;
	case Type::RemoveChild: return parent.getArray()->removeAllInstancesOf(oldValue) > 0;
	default: return false;
	}
}

bool UndoableVarAction::undo()
{
	switch(actionType)
	{
	case Type::SetProperty: parent.getDynamicObject()->setProperty(key, oldValue); return true;
	case Type::RemoveProperty: parent.getDynamicObject()->setProperty(key, oldValue); return true;
	case Type::AddChild: parent.getArray()->removeAllInstancesOf(newValue); return true;
	case Type::RemoveChild: parent.getArray()->insert(index, oldValue);
	default: ;
	}
}
}
}