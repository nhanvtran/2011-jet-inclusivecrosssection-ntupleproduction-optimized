

// Forked from SMPJ Analysis Framework
// https://twiki.cern.ch/twiki/bin/viewauth/CMS/SMPJAnalysisFW
// https://github.com/cms-smpj/SMPJ/tree/v1.0


#include <iostream>
#include <sstream>
#include <istream>
#include <fstream>
#include <iomanip>
#include <string>
#include <cmath>
#include <functional>
#include "TTree.h"
#include <vector>
#include <cassert>
#include <TLorentzVector.h>
#include <time.h>


// c2numpy convertion include
#include "2011-jet-inclusivecrosssection-ntupleproduction-optimized/AnalysisFW/interface/c2numpy.h"
#include "OpenDataTreeProducerOptimized.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Common/interface/TriggerNames.h"
#include "FWCore/Common/interface/TriggerResultsByName.h"

#include "DataFormats/VertexReco/interface/Vertex.h"
#include "DataFormats/VertexReco/interface/VertexFwd.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/Math/interface/deltaR.h"
#include "DataFormats/JetReco/interface/Jet.h"
#include "DataFormats/JetReco/interface/PFJet.h"
#include "DataFormats/JetReco/interface/PFJetCollection.h"
#include "DataFormats/JetReco/interface/GenJet.h"
#include "DataFormats/JetReco/interface/GenJetCollection.h"
#include "DataFormats/JetReco/interface/JetExtendedAssociation.h"
#include "DataFormats/JetReco/interface/JetID.h"
#include "DataFormats/METReco/interface/PFMET.h"
#include "DataFormats/METReco/interface/PFMETCollection.h"
#include "DataFormats/BeamSpot/interface/BeamSpot.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/ParticleFlowCandidate/interface/PFCandidate.h"

#include "SimDataFormats/GeneratorProducts/interface/GenEventInfoProduct.h"
#include "SimDataFormats/GeneratorProducts/interface/GenRunInfoProduct.h"
#include "SimDataFormats/PileupSummaryInfo/interface/PileupSummaryInfo.h"

#include "CondFormats/JetMETObjects/interface/JetCorrectorParameters.h"
#include "JetMETCorrections/Objects/interface/JetCorrectionsRecord.h"

#include "RecoJets/JetAssociationProducers/src/JetTracksAssociatorAtVertex.h"

#include "tensorflow/core/public/session.h"
#include "tensorflow/core/graph/default_device.h"

// #include "fastjet/contrib/SoftDrop.hh"

OpenDataTreeProducerOptimized::OpenDataTreeProducerOptimized(edm::ParameterSet const &cfg) :
  JetTag_(cfg.getParameter<edm::InputTag>("JetTag")),
  JetTok_(consumes<edm::View<pat::Jet>>(JetTag_))
{
}

void OpenDataTreeProducerOptimized::beginJob() {
        
    // load the graph 
    std::cout << "[OpenDataTreeProducerOptimized::beginJob] Loading the .pb files..." << std::endl;
    tensorflow::setLogging();
    
    graphDefFeaturizer_ = tensorflow::loadGraphDef("resnet50.pb");
    std::cout << "featurizer node size = " << graphDefFeaturizer_->node_size() << std::endl;
    // Don't print this out -- it's humongous
    // for (int i = 0; i < graphDefFeaturizer_->node_size(); i++) {
    //   std::cout << graphDefFeaturizer_->node(i).name() << std::endl;
    //   // std::cout << "name = " << name << std::endl;
    // }
    auto shape0F = graphDefFeaturizer_->node().Get(0).attr().at("shape").shape();
    std::cout << "featurizer shape0 size = " << shape0F.dim_size() << std::endl;
    for (int i = 0; i < shape0F.dim_size(); i++) {
      std::cout << shape0F.dim(i).size() << std::endl;
      // std::cout << "name = " << name << std::endl;
    }

    // ReadBinaryProto(tensorflow::Env::Default(), "resnet50_classifier.pb", &graphDef_);
    graphDefClassifier_ = tensorflow::loadGraphDef("resnet50_classifier.pb");
    std::cout << "classifier node size = " << graphDefClassifier_->node_size() << std::endl;
    for (int i = 0; i < graphDefClassifier_->node_size(); i++) {
      std::cout << graphDefClassifier_->node(i).name() << std::endl;
      // std::cout << "name = " << name << std::endl;
    }
    auto shape0C = graphDefClassifier_->node().Get(0).attr().at("shape").shape();
    std::cout << "classifier hape0 size = " << shape0C.dim_size() << std::endl;
    for (int i = 0; i < shape0C.dim_size(); i++) {
      std::cout << shape0C.dim(i).size() << std::endl;
      // std::cout << "name = " << name << std::endl;
    }
    
    // apparently the last node does not have a shape, so this is all commented out
    // auto shapeN = graphDef_->node().Get(graphDef_->node_size()-1).attr().at("shape").shape();    
    // std::cout << "shapeN size = " << shapeN.dim_size() << std::endl;
    // for (int i = 0; i < shapeN.dim_size(); i++) {
    //   std::cout << shapeN.dim(i).size() << std::endl;
    //   // std::cout << "name = " << name << std::endl;
    // }

    time_t my_time = time(NULL);
    printf("%s", ctime(&my_time));        
}

void OpenDataTreeProducerOptimized::endJob() {
}


void OpenDataTreeProducerOptimized::beginRun(edm::Run const &iRun,edm::EventSetup const &iSetup) { 
}


void OpenDataTreeProducerOptimized::analyze(edm::Event const &event_obj,
                                    edm::EventSetup const &iSetup) {
  
    edm::Handle<edm::View<pat::Jet>> h_jets;
    event_obj.getByToken(JetTok_, h_jets);

    // create a jet image for the leading jet in the event
    // 224 x 224 image which is centered at the jet axis and +/- 1 unit in eta and phi
    float image2D[224][224];
    float pixel_width = 2./224.;
    for (int ii = 0; ii < 224; ii++){
      for (int ij = 0; ij < 224; ij++){ image2D[ii][ij] = 0.; }
    }
    
    int jet_ctr = 0;
    for(const auto& i_jet : *(h_jets.product())){
      
      //jet calcs
      float jet_pt  =  i_jet.pt();
      float jet_phi =  i_jet.phi();
      float jet_eta =  i_jet.eta();

      for(unsigned k = 0; k < i_jet.numberOfDaughters(); ++k){

        const reco::Candidate* i_part = i_jet.daughter(k);
        // daughter info
        float i_pt = i_part->pt();
        float i_phi = i_part->phi();
        float i_eta = i_part->eta();
        //std::cout << "daughter pt = " << i_pt << std::endl;
        
        float dphi = i_phi - jet_phi;
        if (dphi > M_PI) dphi -= 2*M_PI;
        if (dphi < -1.*M_PI) dphi += 2*M_PI;
        float deta = i_eta - jet_eta;

        if ( deta > 1. || deta < -1. || dphi > 1. || dphi < 1.) continue; // outside of the image, shouldn't happen for AK8 jet!
        int eta_pixel_index =  (int) ((deta + 1.)/pixel_width);
        int phi_pixel_index =  (int) ((dphi + 1.)/pixel_width);
        image2D[eta_pixel_index][phi_pixel_index] += i_pt/jet_pt;

      }
  
      //////////////////////////////
      jet_ctr++;
      if (jet_ctr > 0) break; // just do one jet for now
      //////////////////////////////

    }

    std::cout << "----------------------------------------------------------------------" << std::endl;
    // --------------------------------------------------------------------
    // Run the Featurizer
    std::cout << " ====> Run the Featurizer..." << std::endl;
    // Tensorflow part
    std::cout << "Create featurizer session..." << std::endl;
    tensorflow::Session* sessionF = tensorflow::createSession(graphDefFeaturizer_);
    // convert image to tensor
    tensorflow::Tensor inputImage(tensorflow::DT_FLOAT, { 1, 224, 224, 3 });
    // std::cout << "inputImage shape = " << inputImage.tensor<float,(4)>() << std::endl;
    auto input_map = inputImage.tensor<float, 4>();
    for (int itf = 0; itf < 224; itf++){
      for (int jtf = 0; jtf < 224; jtf++){
        // input_map(0,itf,jtf,0) = image2D[itf][jtf];
        // input_map(0,itf,jtf,1) = image2D[itf][jtf];
        // input_map(0,itf,jtf,2) = image2D[itf][jtf];
        input_map(0,itf,jtf,0) = (float) 0.1*itf + 0.1*jtf;
        input_map(0,itf,jtf,1) = (float) 0.2*itf + 0.2*jtf;
        input_map(0,itf,jtf,2) = (float) 0.3*itf + 0.3*jtf;
      }
    }
    std::cout << "Featurizer input = " << inputImage.DebugString() << endl;

    std::vector<tensorflow::Tensor> featurizer_outputs;
    tensorflow::Status statusF = sessionF->Run( {{"InputImage:0",inputImage}}, { "resnet_v1_50/pool5:0" }, {}, &featurizer_outputs);
    if (!statusF.ok()) { std::cout << statusF.ToString() << std::endl; }
    else{ std::cout << "Featurizer Status: Ok" << std::endl; }
    std::cout << "featurizer_outputs vector size = " << featurizer_outputs.size() << std::endl;
    std::cout << "featurizer_outputs vector = " << featurizer_outputs[0].DebugString() << std::endl;

    time_t my_time2 = time(NULL);
    printf("Post Featurizer Time %s", ctime(&my_time2));

    std::cout << "Close the featurizer session..." << std::endl;
    tensorflow::closeSession(sessionF);    


    // --------------------------------------------------------------------
    // Run the Classifier
    std::cout << " ====> Run the Classifier..." << std::endl;
    // Tensorflow part
    std::cout << "Create classifier session..." << std::endl;
    tensorflow::Session* sessionC = tensorflow::createSession(graphDefClassifier_);

    tensorflow::Tensor inputClassifer(tensorflow::DT_FLOAT, { 1, 1, 1, 2048 });
    auto input_map_classifier = inputClassifer.tensor<float,4>();
    for (int itf = 0; itf < 2048; itf++){
      input_map_classifier(0,0,0,itf) = (float) itf * 0.1;
    }
    std::cout << "Classifier input = " << inputClassifer.DebugString() << endl;

    std::vector<tensorflow::Tensor> outputs;
    tensorflow::Status statusC = sessionC->Run( {{"Input:0",inputClassifer}}, { "resnet_v1_50/logits/Softmax:0" }, {}, &outputs);
    if (!statusC.ok()) { std::cout << statusC.ToString() << std::endl; }
    else{ std::cout << "Classifier Status: Ok" << std::endl; }
    // auto outputs_map_classifier = outputs[0].tensor<float,4>();
    std::cout << "output vector size = " << outputs.size() << std::endl;
    std::cout << "output vector = " << outputs[0].DebugString() << std::endl;

    time_t my_time3 = time(NULL);
    printf("Post Classifer Time: %s", ctime(&my_time3));

    std::cout << "Close the classifier session..." << std::endl;
    tensorflow::closeSession(sessionC);    
    // --------------------------------------------------------------------



}



void OpenDataTreeProducerOptimized::endRun(edm::Run const &iRun, edm::EventSetup const &iSetup) {

}

OpenDataTreeProducerOptimized::~OpenDataTreeProducerOptimized() {
}


DEFINE_FWK_MODULE(OpenDataTreeProducerOptimized);
