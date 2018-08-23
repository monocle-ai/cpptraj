#include <algorithm> // std::min, std::max
#include "Analysis_State.h"
#include "CpptrajStdio.h"

Analysis_State::Analysis_State() :
  state_data_(0),
  curveOut_(0),
  stateOut_(0),
  transOut_(0),
  countOut_(0),
  debug_(0)
{}

void Analysis_State::Help() const {
  mprintf("\t{state <ID>,<dataset>,<min>,<max>} [out <state v time file>] [name <setname>]\n"
          "\t[curveout <curve file>] [stateout <states file>] [transout <transitions file>]\n"
          "  Data for the specified data set(s) that matches the given criteria\n"
          "  will be assigned state <#>.\n");
}

// Analysis_State::Setup()
Analysis::RetType Analysis_State::Setup(ArgList& analyzeArgs, AnalysisSetup& setup, int debugIn)
{
  debug_ = debugIn;
  masterDSL_ = setup.DslPtr();
  DataFile* outfile = setup.DFL().AddDataFile( analyzeArgs.GetStringKey("out"), analyzeArgs );
  curveOut_ = setup.DFL().AddDataFile( analyzeArgs.GetStringKey("curveout"), analyzeArgs );
  stateOut_ = setup.DFL().AddCpptrajFile(analyzeArgs.GetStringKey("stateout"),
                                         "State Lifetime Output", DataFileList::TEXT, true);
  transOut_ = setup.DFL().AddCpptrajFile(analyzeArgs.GetStringKey("transout"),
                                         "Transitions Output", DataFileList::TEXT, true);
  countOut_ = setup.DFL().AddCpptrajFile(analyzeArgs.GetStringKey("countout"),
                                         "State Count Output", DataFileList::TEXT, true);
  normalize_ = analyzeArgs.hasKey("norm");
  // Get definitions of states if present.
  // Define states as 'state <#>,<dataset>,<min>,<max>'
  std::string state_arg = analyzeArgs.GetStringKey("state");
  if (!state_arg.empty()) {
    while (!state_arg.empty()) {
      // Expect form <#>,<dataset>
      ArgList argtmp(state_arg, ",");
      if (argtmp.Nargs() < 4) {
        mprinterr("Error: Malformed state argument '%s': expect <ID>,<dataset>,<min>,<max>\n",
                  state_arg.c_str());
        return Analysis::ERR;
      } else if (((argtmp.Nargs()-1) % 3) != 0) {
        mprinterr("Error: Each state <dataset> arg must have a <min> and <max> value.\n");
        return Analysis::ERR;
      }
      std::string state_id = argtmp.GetStringNext();
      // TODO: Check duplicate names
      if (state_id.empty()) {
        mprinterr("Error: In state argument, could not get ID.\n");
        return Analysis::ERR;
      }
      // Determine state number
      int state_num = -1;
      IdxMapType::const_iterator it = NameMap_.find( state_id );
      if (it == NameMap_.end()) {
        state_num = NameMap_.size();
        NameMap_.insert( IdxPair( state_id, state_num ) );
        // For mapping state number back to name
        StateNames_.push_back( state_id );
      } else
        state_num = it->second;
      StateType currentState( state_id, state_num );
      int nSets = (argtmp.Nargs()-1) / 3;
      for (int setArg = 0; setArg < nSets; setArg++) {
        DataSet* ds = setup.DSL().GetDataSet( argtmp.GetStringNext() );
        if (ds == 0) return Analysis::ERR;
        if (ds->Ndim() != 1) {
          mprinterr("Error: Only 1D data sets allowed.\n");
          return Analysis::ERR;
        }
        double min = argtmp.getNextDouble(0.0);
        double max = argtmp.getNextDouble(0.0);
        if (max < min) {
          mprinterr("Error: max value cannot be less than min.\n");
          return Analysis::ERR;
        }
        currentState.AddCriterion( (DataSet_1D*)ds, min, max );
      }
      States_.push_back( currentState );
      state_arg = analyzeArgs.GetStringKey("state");
    }
  }
  if (States_.empty()) {
    mprinterr("Error: No states defined.\n");
    return Analysis::ERR;
  }
  state_data_ = setup.DSL().AddSet(DataSet::INTEGER, analyzeArgs.GetStringKey("name"), "State");
  if (state_data_ == 0) return Analysis::ERR;
  if (outfile != 0) outfile->AddDataSet( state_data_ );
  // Set up state data sets
  Dimension Xdim(-1, 1, "Index");
  DataSet* ds = setup.DSL().AddSet(DataSet::INTEGER,
                                   MetaData(state_data_->Meta().Name(), "Count"));
  if (ds == 0) return Analysis::ERR;
  ds->SetDim(0, Xdim);
  state_counts_ = ds;

  mprintf("    STATE: The following state definitions have been set up:\n");
  for (StateArray::const_iterator state = States_.begin(); state != States_.end(); ++state)
  {
    mprintf("\t%u: ", state - States_.begin());
    state->PrintState();
  }
  mprintf("\tState data set: %s\n", state_data_->legend());
  if (outfile != 0)
    mprintf("\tStates vs time output to file '%s'\n", outfile->DataFilename().full());
  if (curveOut_ != 0)
    mprintf("\tCurves output to file '%s'\n", curveOut_->DataFilename().full());
  mprintf("\tState lifetime output to file '%s'\n", stateOut_->Filename().full());
  mprintf("\tTransitions output to file '%s'\n", transOut_->Filename().full());
  mprintf("\tState counts output to file '%s'\n", countOut_->Filename().full());
  if (normalize_)
    mprintf("\tCurves will be normalized to 1.0\n");

  return Analysis::OK;
}

/** Default state name for state -1 */
std::string Analysis_State::UNDEFINED_ = "Undefined";

// Analysis_State::StateName()
std::string const& Analysis_State::StateName(int idx) const {
  if (idx > -1)
    return StateNames_[idx];
  else // Should never be called with any other negative number but -1
    return UNDEFINED_;
}

//  Analysis_State::Analyze()
Analysis::RetType Analysis_State::Analyze() {
  // Only process as much data as is in the smallest data set.
  size_t nframes = States_.front().Nframes();
  for (StateArray::const_iterator state = States_.begin(); state != States_.end(); ++state)
  {
    if ( state != States_.begin() ) {
      size_t nf = state->Nframes();
      if (nframes != nf) {
        mprintf("Warning: State '%s' has a different # of frames (%zu) "
                "than previous states (%zu).\n", state->id(), nf, nframes);
        nframes = std::min( nframes, nf );
      }
    }
  }
  mprintf("\tProcessing %zu frames.\n", nframes);
  if (nframes < 1) return Analysis::ERR;

  std::vector<Transition> Status;
  int numStates = (int)NameMap_.size() + 1; // + 1 for state -1 (undefined)
  Status.reserve( numStates );
  for (int i = 0; i != numStates; i++) {
    DataSet* ds = masterDSL_->AddSet(DataSet::DOUBLE, 
                                     MetaData(state_data_->Meta().Name(), "sCurve", i));
    if (ds == 0) return Analysis::ERR;
    if (curveOut_ != 0) curveOut_->AddDataSet( ds );
    ds->SetLegend( StateName(i-1) );
    Status.push_back( Transition((DataSet_double*)ds) );
  }

  int last_state = -2;
  size_t last_State_Start = 0;
  size_t final_frame = nframes - 1;
  std::vector<unsigned int> stateFrames(numStates, 0);
  for (size_t frm = 0; frm != nframes; frm++) {
    // Determine which state we are in.
    int state_num = -1;
    for (StateArray::const_iterator state = States_.begin(); state != States_.end(); ++state)
    {
      if (state->InState( frm )) {
        if (state_num != -1) {
          mprintf("Warning: Frame %zu already defined as state '%s', also matches state '%s'.\n",
                  frm, States_[state_num].id(), state->id());
          mprintf("Warning: Overriding previous state.\n");
          state_num = state->Num();
        } else
          state_num = state->Num();
      }
    }
    state_data_->Add( frm, &state_num );
    stateFrames[state_num+1]++;

    // Determine if there has been a transition.
    if (last_state == -2)
      // First frame, no possible transition yet.
      last_state = state_num;
    else if (state_num != last_state || frm == final_frame) {
      int length = (int)(frm - last_State_Start);
      if (state_num != last_state) {
        // There has been a transition from last_state to state_num.
        StatePair sPair(last_state, state_num);
        TransMapType::iterator entry = TransitionMap_.find( sPair );
        if (entry == TransitionMap_.end()) {
          // New transition
          DataSet* ds = masterDSL_->AddSet( DataSet::DOUBLE,
                                            MetaData(state_data_->Meta().Name(), "tCurve",
                                                     TransitionMap_.size()) );
          if (ds == 0) return Analysis::ERR;
          if (curveOut_ != 0) curveOut_->AddDataSet( ds );
          ds->SetLegend( StateName(last_state) + "->" + StateName(state_num) );
          TransitionMap_.insert( TransPair(sPair, Transition(length, (DataSet_double*)ds)) );
        } else
          // Update previous transition
          entry->second.Update(length);
      }
      // Update single state information.
      Status[last_state + 1].Update(length); 
      last_State_Start = frm;
      last_state = state_num;
    }
  }

  // DEBUG: Print single state info.
  if (debug_ > 0) {
    mprintf("  States:\n");
    mprintf("\t%i: %s  max= %i  sum= %i  n= %i  Avg= %g\n", -1, "Undefined",
            Status.front().Max(), Status.front().Sum(),
            Status.front().Nlifetimes(), Status.front().Avg());
    StateArray::const_iterator state = States_.begin();
    for (std::vector<Transition>::const_iterator s = Status.begin() + 1;
                                                 s != Status.end(); ++s, ++state)
    mprintf("\t%u: %s  max= %i  sum= %i  n= %i  Avg= %g\n", state - States_.begin(),
            state->id(), s->Max(), s->Sum(), s->Nlifetimes(), s->Avg());
    // DEBUG: Print transitions.
    mprintf("  Transitions:\n");
    for (TransMapType::const_iterator trans = TransitionMap_.begin();
                                      trans != TransitionMap_.end(); ++trans)
      mprintf("\t%i -> %i: max= %i  sum= %i  n= %i  Avg= %g\n", trans->first.first,
              trans->first.second, trans->second.Max(), trans->second.Sum(),
              trans->second.Nlifetimes(), trans->second.Avg());
  }
  countOut_->Printf("%-8s %12s %12s %s\n", "#Index", "Count", "Frac", "State");
  for (int idx = 0; idx != (int)stateFrames.size(); idx++) {
    countOut_->Printf("%-8i %12i %12.4f %s\n", idx-1, stateFrames[idx],
                      (double)stateFrames[idx]/(double)nframes, stateName(idx-1));
    state_counts_->Add(idx, &(stateFrames[idx]));
  }
  stateOut_->Printf("%-8s %12s %12s %12s %s\n", "#Index", "N", "Average", "Max", "State");
  for (int idx = 0; idx != (int)Status.size(); idx++)
    stateOut_->Printf("%-8i %12i %12.4f %12i %s\n", idx-1, Status[idx].Nlifetimes(),
                      Status[idx].Avg(), Status[idx].Max(), stateName(idx-1));
  transOut_->Printf("%-12s %12s %12s %s\n", "#N", "Average", "Max", "Transition");
  for (TransMapType::const_iterator trans = TransitionMap_.begin();
                                    trans != TransitionMap_.end(); ++trans)
    transOut_->Printf("%-12i %12.4f %12i %s\n", trans->second.Nlifetimes(),
                      trans->second.Avg(), trans->second.Max(), 
                      trans->second.DS().legend());
  if (normalize_) {
    for (std::vector<Transition>::const_iterator s = Status.begin();
                                                 s != Status.end(); ++s)
      s->NormalizeCurve();
    for (TransMapType::const_iterator trans = TransitionMap_.begin();
                                      trans != TransitionMap_.end(); ++trans)
      trans->second.NormalizeCurve();
  }
  return Analysis::OK;
}

// ----- Analysis_State::StateType ---------------------------------------------
size_t Analysis_State::StateType::Nframes() const
{
  if (Sets_.empty()) return 0;
  size_t nframes = Sets_[0]->Size();
  for (unsigned int idx = 1; idx < Sets_.size(); idx++)
    if (nframes != Sets_[idx]->Size()) {
      mprintf("Warning: State '%s', set '%s' has differing # frames (%zu).",
              id(), Sets_[idx]->legend(), Sets_[idx]->Size());
      nframes = std::min( nframes, Sets_[idx]->Size() );
      mprintf(" Only using %zu frames.\n", nframes);
    }
  return nframes;
}

void Analysis_State::StateType::PrintState() const {
  mprintf("%s (%i)", id(), num_);
  for (unsigned int idx = 0; idx != Sets_.size(); idx++)
    mprintf(" {%.4f <= %s < %.4f}", Min_[idx], Sets_[idx]->legend(), Max_[idx]);
  mprintf("\n");
}
