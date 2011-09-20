/* Action: Clustering
 */
#include "Action_Clustering.h"
#include "CpptrajStdio.h"
#include "ProgressBar.h"
#include "TrajectoryFile.h"
#include <cfloat>
#include <cstdio> // sprintf
#include <cstring> // strlen

// CONSTRUCTOR
Clustering::Clustering() {
  //fprintf(stderr,"Clustering Con\n");
  useMass=false;
  Linkage = ClusterList::AVERAGELINK;
  cnumvtime=NULL;
} 

// DESTRUCTOR
Clustering::~Clustering() { 
}

/* Clustering::init()
 * Expected call: cluster [<mask>] [mass] [clusters <n>] [epsilon <e>] [out <cnumvtime>]
 *                        [ linkage | averagelinkage | complete ]  
 *                        [summary <summaryfile>] 
 *                        [ clusterout <trajfileprefix> [clusterfmt <trajformat>] ] 
 *                        [ singlerepout <trajfilename> [singlerepfmt <trajformat>] ]
 * Dataset name will be the last arg checked for. Check order is:
 *    1) Keywords
 *    2) Masks
 *    3) Dataset name
 */
int Clustering::init() {
  char *mask0,*cnumvtimefile,*clusterformat,*singlerepformat;
  // NOTE: PtrajFile is here just for determining/writing format. Should those
  //       functions be separate?
  PtrajFile TempFile;

  // Get keywords
  useMass = A->hasKey("mass");
  targetNclusters = A->getKeyInt("clusters",-1);
  epsilon = A->getKeyDouble("epsilon",-1.0);
  if (A->hasKey("linkage")) Linkage=ClusterList::SINGLELINK;
  if (A->hasKey("averagelinkage")) Linkage=ClusterList::AVERAGELINK;
  if (A->hasKey("complete")) Linkage=ClusterList::COMPLETELINK;
  cnumvtimefile = A->getKeyString("out",NULL);
  summaryfile = A->getKeyString("summary",NULL);
  // Output trajectory stuff
  clusterfile = A->getKeyString("clusterout",NULL);
  clusterformat = A->getKeyString("clusterfmt",NULL);
  singlerepfile = A->getKeyString("singlerepout",NULL);
  singlerepformat = A->getKeyString("singlerepfmt",NULL);

  // Figure out trajectory formats
  if (clusterfile!=NULL) {
    clusterfmt = TempFile.GetFmtFromArg(clusterformat,AMBERTRAJ);
  }
  if (singlerepfile!=NULL) {
    singlerepfmt = TempFile.GetFmtFromArg(singlerepformat,AMBERTRAJ);
  }

  // Get the mask string 
  mask0 = A->getNextMask();
  Mask0.SetMaskString(mask0);

  // Dataset to store cluster number v time
  cnumvtime = DSL->Add(INT,A->getNextString(),"Cnum");
  if (cnumvtime==NULL) return 1;
  // Add dataset to data file list
  DFL->Add(cnumvtimefile,cnumvtime);

  // Determine finish criteria. If nothing specified default to 10 clusters.
  if (targetNclusters==-1 && epsilon==-1.0)
    targetNclusters = 10;

  mprintf("    CLUSTER: (%s) ",Mask0.maskString);
  if (useMass)
    mprintf(" (mass-weighted)");
  if (targetNclusters != -1)
    mprintf(" looking for %i clusters",targetNclusters);
  if (epsilon != -1.0)
    mprintf(" epsilon is %8.3lf",epsilon);
  mprintf("\n");
  mprintf("            Using hierarchical top-down clustering algorithm,");
  if (Linkage==ClusterList::SINGLELINK)
    mprintf(" single-linkage");
  else if (Linkage==ClusterList::AVERAGELINK)
    mprintf(" average-linkage");
  else if (Linkage==ClusterList::COMPLETELINK)
    mprintf(" complete-linkage");
  mprintf(".\n");
  if (summaryfile!=NULL)
    mprintf("            Summary of cluster results will be written to %s\n",summaryfile);
  if (clusterfile!=NULL)
    mprintf("            Cluster trajectories will be written to %s, format %s\n",
            clusterfile,TempFile.Format(clusterfmt));
  if (singlerepfile!=NULL)
    mprintf("            Cluster representatives will be written to 1 traj (%s), format %s\n",
            singlerepfile,TempFile.Format(singlerepfmt));

  // If epsilon not given make it huge 
  if (epsilon == -1.0) epsilon = DBL_MAX;
  // if target clusters not given make it 1
  if (targetNclusters == -1) targetNclusters=1;

  return 0;
}

/* Clustering::action()
 * Store current frame as a reference frame.
 */
int Clustering::action() {
  Frame *fCopy;

  fCopy = F->Copy();
  ReferenceFrames.Add(fCopy,P);
  
  return 0;
} 

/* Clustering::calcDistFromRmsd()
 */
int Clustering::calcDistFromRmsd( TriangleMatrix *Distances) {
  // Reference
  AmberParm *RefParm = NULL;
  Frame *RefFrame = NULL;
  Frame *SelectedRef=NULL;
  int lastrefpindex=-1;
  int refatoms = 0;
  // Target
  AmberParm *TgtParm;
  Frame *TgtFrame;
  Frame *SelectedTgt=NULL;
  int lasttgtpindex=-1;
  // Other vars
  double R;
  int current=0;
  int max=0;
  int totalref=0;

  totalref = ReferenceFrames.NumFrames();
  Distances->Setup(totalref);

  max = Distances->Nelements();
  mprintf("  CLUSTER: Calculating RMSDs between each frame (%i total).\n  ",max);

  // Set up progress Bar
  ProgressBar *progress = new ProgressBar(max);

  // LOOP OVER REFERENCE FRAMES
  for (int nref=0; nref < totalref - 1; nref++) {
    progress->Update(current);
    RefParm = ReferenceFrames.GetFrameParm( nref );
    // If the current ref parm not same as last ref parm, reset reference mask
    if (RefParm->pindex != lastrefpindex) {
      if ( Mask0.SetupMask(RefParm,debug) ) {
        mprinterr("Error: Clustering: Could not set up reference mask for %s\n",RefParm->parmName);
        if (SelectedRef!=NULL) delete SelectedRef;
        if (SelectedTgt!=NULL) delete SelectedTgt;
        return 1;
      }
      refatoms = Mask0.Nselected;
      if ( SelectedRef!=NULL ) delete SelectedRef;
      SelectedRef = new Frame(&Mask0, RefParm->mass);
      lastrefpindex = RefParm->pindex;
    }
    // Get the current reference frame
    RefFrame = ReferenceFrames.GetFrame( nref );

    // LOOP OVER TARGET FRAMES
    for (int nframe=nref+1; nframe < totalref; nframe++) {
      TgtParm = ReferenceFrames.GetFrameParm( nframe );
      // If the current frame parm not same as last frame parm, reset frame mask
      if (TgtParm->pindex != lasttgtpindex) {
        if ( Mask0.SetupMask(TgtParm,debug) ) {
          mprinterr("Error: Clustering: Could not set up target mask for %s\n",TgtParm->parmName);
          if (SelectedRef!=NULL) delete SelectedRef;
          if (SelectedTgt!=NULL) delete SelectedTgt;
          return 1;
        }
        // Check that num atoms in mask are the same
        if (Mask0.Nselected != refatoms) {
          mprinterr(
            "Error: Clustering: Num atoms in target mask (%i) != num atoms in ref mask (%i)\n",
                    Mask0.Nselected, refatoms);
          if (SelectedRef!=NULL) delete SelectedRef;
          if (SelectedTgt!=NULL) delete SelectedTgt;
          return 1;
        }
        if ( SelectedTgt!=NULL ) delete SelectedTgt;
        SelectedTgt = new Frame(&Mask0, TgtParm->mass);
        lasttgtpindex = TgtParm->pindex;
      }
      // Get the current target frame
      TgtFrame = ReferenceFrames.GetFrame( nframe );

      // Set selected reference atoms - always done since RMS fit modifies SelectedRef
      SelectedRef->SetFrameCoordsFromMask(RefFrame->X, &Mask0);
      // Set selected target atoms
      SelectedTgt->SetFrameCoordsFromMask(TgtFrame->X, &Mask0);

      // Perform RMS calculation
      R = SelectedTgt->RMSD(SelectedRef, useMass);

      Distances->AddElement( R );
      // DEBUG
      //mprinterr("%12i %12i %12.4lf\n",nref,nframe,R);
      current++;
    } // END loop over target frames
  } // END loop over reference frames
  progress->Update(max);
  delete progress;

  if (SelectedRef!=NULL) delete SelectedRef;
  if (SelectedTgt!=NULL) delete SelectedTgt;

  
  return 0;
}

/* Clustering::ClusterHierAgglo()
 * Cluster using a hierarchical agglomerative (bottom-up) approach. All frames
 * start in their own cluster. The closest two clusters are merged, and 
 * distances between the newly merged cluster and all remaining clusters are
 * recalculated according to one of the following metrics:
 *   single-linkage  : The minimum distance between frames in clusters are used.
 *   average-linkage : The average distance between frames in clusters are used.
 *   complete-linkage: The maximum distance between frames in clusters are used.
 */
int Clustering::ClusterHierAgglo( TriangleMatrix *FrameDistances, ClusterList *CList) {
  std::list<int> frames;
  bool clusteringComplete = false;
  int iterations = 0;

  // Build initial clusters.
  for (int cluster = 0; cluster < FrameDistances->Nrows(); cluster++) {
    frames.assign(1,cluster);
    CList->AddCluster(&frames,cluster);
  }
  // Build initial cluster distance matrix.
  CList->Initialize( FrameDistances );

  // DEBUG
  if (debug>1) CList->PrintClusters();

  while (!clusteringComplete) {
    // Merge 2 closest clusters
    if (CList->MergeClosest(epsilon)) break;

    // Check if clustering is complete.
    // If the target number of clusters is reached we are done
    if (CList->Nclusters() <= targetNclusters) {
      mprintf("\tTarget # of clusters (%i) met (%u), clustering complete.\n",targetNclusters,
              CList->Nclusters());
      break;
    } 
    if (CList->Nclusters() == 1) clusteringComplete = true; // Sanity check
    iterations++;
  }
  mprintf("\tCLUSTER: Completed after %i iterations, %u clusters.\n",iterations,
          CList->Nclusters());

  return 0;
}

/* Clustering::CreateCnumvtime()
 * Put cluster number vs frame into dataset.
 */
void Clustering::CreateCnumvtime( ClusterList *CList ) {
  std::list<int>::iterator E;
  int cnum;

  CList->Begin();
  while (!CList->End()) {
    //mprinterr("Cluster %i:\n",CList->CurrentNum());
    cnum = CList->CurrentNum();
    E = CList->CurrentFrameEnd();
    for (std::list<int>::iterator frame = CList->CurrentFrameBegin();
                                  frame != E;
                                  frame++)
    {
      //mprinterr("%i,",*frame);
      cnumvtime->Add( *frame, &cnum );
    }
    //mprinterr("\n");
    CList->NextCluster();
    //break;
  }
}

/* Clustering::WriteClusterTraj()
 * Write frames in each cluster to a trajectory file.
 */
void Clustering::WriteClusterTraj( ClusterList *CList ) {
  std::list<int>::iterator E;
  std::list<int>::iterator B;
  char *cfilename;
  int cnum,framenum;
  TrajectoryFile *clusterout = NULL;
  AmberParm *clusterparm;
  Frame *clusterframe;

  // Figure out max size of cluster filename
  cnum = CList->Nclusters();
  cnum = (cnum / 10) + 3;
  cfilename = new char[ strlen(clusterfile)+cnum+1 ];

  CList->Begin();
  while (!CList->End()) {
    // Create filename based on cluster number.
    cnum = CList->CurrentNum();
    sprintf(cfilename,"%s.c%i",clusterfile,cnum);
    // Set up trajectory file - use parm from first frame of cluster (pot. dangerous)
    if (clusterout!=NULL) delete clusterout;
    clusterout = new TrajectoryFile;
    B = CList->CurrentFrameBegin();
    clusterparm = ReferenceFrames.GetFrameParm( *B );
    if (clusterout->SetupWrite(cfilename,NULL,clusterparm,clusterfmt)) {
      mprinterr("Error: Clustering::WriteClusterTraj: Could not set up %s for write.\n",
                cfilename);
      delete clusterout;
      delete[] cfilename;
      return;
    }
    //mprinterr("Cluster %i:\n",CList->CurrentNum());
    
    E = CList->CurrentFrameEnd();
    framenum = 0;
    for (std::list<int>::iterator frame = B; frame != E; frame++)
    {
      //mprinterr("%i,",*frame);
      clusterframe = ReferenceFrames.GetFrame( *frame );
      clusterout->WriteFrame(framenum++, clusterparm, clusterframe->X, NULL, 
                             clusterframe->box, clusterframe->T);
    }
    // Close traj
    clusterout->EndTraj();
    //mprinterr("\n");
    CList->NextCluster();
    //break;
  }
  delete[] cfilename;
  if (clusterout!=NULL) delete clusterout;
}

/* Clustering::WriteSingleRepTraj()
 * Write representative frame of each cluster to a trajectory file.
 */
void Clustering::WriteSingleRepTraj( ClusterList *CList ) {
  int framenum, framecounter;
  TrajectoryFile clusterout;
  AmberParm *clusterparm;
  Frame *clusterframe;

  // Find centroid of first cluster in order to set up parm
  // NOTE: This is redundant if the Summary routine has already been called.
  CList->Begin();
  framenum = CList->CurrentCentroid();

  // Set up trajectory file. Use parm from first frame of cluster (pot. dangerous)
  clusterparm = ReferenceFrames.GetFrameParm( framenum );
  if (clusterout.SetupWrite(singlerepfile,NULL,clusterparm,singlerepfmt)) {
    mprinterr("Error: Clustering::WriteSingleRepTraj: Could not set up %s for write.\n",
                singlerepfile);
     return;
  }
  // Write first cluster rep frame
  framecounter=0;
  clusterframe = ReferenceFrames.GetFrame( framenum );
  clusterout.WriteFrame(framecounter++, clusterparm, clusterframe->X, NULL,
                        clusterframe->box, clusterframe->T);

  CList->NextCluster();
  while (!CList->End()) {
    //mprinterr("Cluster %i: ",CList->CurrentNum());
   framenum = CList->CurrentCentroid();
   //mprinterr("%i\n",framenum);
   clusterframe = ReferenceFrames.GetFrame( framenum );
   clusterout.WriteFrame(framecounter++, clusterparm, clusterframe->X, NULL, 
                         clusterframe->box, clusterframe->T);
    //mprinterr("\n");
    CList->NextCluster();
    //break;
  }
  // Close traj
  clusterout.EndTraj();
}

/* Clustering::print()
 * This is where the clustering is actually performed. First the distances
 * between each frame are calculated. Then the clustering routine is called.
 */
void Clustering::print() {
  TriangleMatrix Distances;
  ClusterList CList;

  // Get distances between frames
  calcDistFromRmsd( &Distances );

  // DEBUG
  if (debug>1) {
    mprintf("INTIAL FRAME DISTANCES:\n");
    Distances.PrintElements();
  }

  // Cluster
  CList.SetLinkage(Linkage);
  ClusterHierAgglo( &Distances, &CList);

  // Sort clusters and renumber
  CList.Renumber();

  // DEBUG
  //if (debug>0) {
    mprintf("\nFINAL CLUSTERS:\n");
    CList.PrintClusters();
  //}

  // Print a summary of clusters
  if (summaryfile!=NULL)
    CList.Summary(summaryfile);

  // Create cluster v time data from clusters.
  CreateCnumvtime( &CList );

  // Write clusters to trajectories
  if (clusterfile!=NULL)
    WriteClusterTraj( &CList ); 

  // Write all representative frames to a single traj
  if (singlerepfile!=NULL)
    WriteSingleRepTraj( &CList );
}
