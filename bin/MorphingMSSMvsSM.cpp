#include "CombineHarvester/CombinePdfs/interface/MorphFunctions.h"
#include "CombineHarvester/CombinePdfs/interface/CMSHistFuncFactory.h"
#include "CombineHarvester/CombineTools/interface/Algorithm.h"
#include "CombineHarvester/CombineTools/interface/AutoRebin.h"
#include "CombineHarvester/CombineTools/interface/BinByBin.h"
#include "CombineHarvester/CombineTools/interface/CardWriter.h"
#include "CombineHarvester/CombineTools/interface/CombineHarvester.h"
#include "CombineHarvester/CombineTools/interface/Observation.h"
#include "CombineHarvester/CombineTools/interface/Process.h"
#include "CombineHarvester/CombineTools/interface/Systematics.h"
#include "CombineHarvester/CombineTools/interface/Utilities.h"
#include "CombineHarvester/MSSMvsSMRun2Legacy/interface/HttSystematics_MSSMvsSMRun2.h"
#include "CombineHarvester/MSSMvsSMRun2Legacy/interface/BinomialBinByBin.h"
#include "CombineHarvester/MSSMvsSMRun2Legacy/interface/dout_tools.h"
#include "RooRealVar.h"
#include "RooWorkspace.h"
#include "TF1.h"
#include "TH2.h"
#include "boost/algorithm/string/predicate.hpp"
#include "boost/algorithm/string/split.hpp"
#include "boost/lexical_cast.hpp"
#include "boost/program_options.hpp"
#include "boost/regex.hpp"
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <math.h>

using namespace std;
using boost::starts_with;
namespace po = boost::program_options;


int main(int argc, char **argv) {
  typedef vector<string> VString;
  typedef vector<pair<int, string>> Categories;
  using ch::syst::bin_id;
  using ch::JoinStr;

  // Define program options
  string output_folder = "output_MSSMvsSM_Run2";
  string base_path = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/MSSMvsSMRun2Legacy/shapes/";
  string sm_gg_fractions = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/MSSMvsSMRun2Legacy/data/higgs_pt_v3_mssm_mode.root";
  string chan = "mt";
  string category = "mt_nobtag_lowmsv_0jet_tightmt";
  string variable = "m_sv_puppi";

  bool auto_rebin = false;
  bool real_data = false;
  bool verbose = false;
  bool mva(false), no_emb(false);

  vector<string> mass_susy_ggH({}), mass_susy_qqH({});

  string analysis = "sm"; // "sm",  "mssm", "mssm_vs_sm_classic", "mssm_vs_sm"
  int era = 2016; // 2016, 2017 or 2018
  po::variables_map vm;
  po::options_description config("configuration");
  config.add_options()
      ("base-path,base_path", po::value<string>(&base_path)->default_value(base_path))
      ("sm_gg_fractions", po::value<string>(&sm_gg_fractions)->default_value(sm_gg_fractions))
      ("channel", po::value<string>(&chan)->default_value(chan))
      ("category", po::value<string>(&category)->default_value(category))
      ("variable", po::value<string>(&variable)->default_value(variable))
      ("auto_rebin", po::value<bool>(&auto_rebin)->default_value(auto_rebin))
      ("real_data", po::value<bool>(&real_data)->default_value(real_data))
      ("verbose", po::value<bool>(&verbose)->default_value(verbose))
      ("output_folder", po::value<string>(&output_folder)->default_value(output_folder))
      ("analysis", po::value<string>(&analysis)->default_value(analysis))
      ("era", po::value<int>(&era)->default_value(era))
      ("no-emb,no-emb,no_emb", po::bool_switch(&no_emb), "use MC samples instead of embedding")
      ("debug,d", po::bool_switch(&debug), "debug printout")
      ("mva", po::bool_switch(&mva), "mva tau id is used")
      ("mass-susy-ggH,mass_susy_ggH", po::value<vector<string>>(&mass_susy_ggH)->multitoken(), "mass_susy_ggH")
      ("mass-susy-qqH,mass_susy_qqH", po::value<vector<string>>(&mass_susy_qqH)->multitoken(), "mass_susy_qqH")
      ("help", "produce help message");
  po::store(po::command_line_parser(argc, argv).options(config).run(), vm);
  po::notify(vm);
  if (vm.count("help"))
  {
      cout << config << "\n";
      return 0;
  }

  // Define the location of the "auxiliaries" directory where we can
  // source the input files containing the datacard shapes
  std::string era_tag;
  if (era == 2016) era_tag = "2016";
  else if (era == 2017) era_tag = "2017";
  else if (era == 2018) era_tag = "2018";
  else std::runtime_error("Given era is not implemented.");

  output_folder = output_folder + "_" + analysis;
  std::map<string, string> input_dir;
  if (base_path.back() != '/' ) base_path += "/";
  if (!boost::filesystem::exists(base_path)) boost::filesystem::create_directories(base_path);
  input_dir["mt"] = base_path + "/" +era_tag + "/mt/";
  input_dir["et"] = base_path + "/" +era_tag + "/et/";
  input_dir["tt"] = base_path + "/" +era_tag + "/tt/";
  input_dir["em"] = base_path + "/" +era_tag + "/em/";

  // Define channels
  VString chns;
  if (chan.find("mt") != std::string::npos)
    chns.push_back("mt");
  if (chan.find("et") != std::string::npos)
    chns.push_back("et");
  if (chan.find("tt") != std::string::npos)
    chns.push_back("tt");
  if (chan.find("em") != std::string::npos)
    chns.push_back("em");

  // Define restriction to the channel defined by '--category' option
  if(category != "all"){
    std::vector<std::string> category_split;
    boost::split(category_split, category, boost::is_any_of("_"));
    chns = {category_split.at(0)};
  }
  doutnonl("Channels:\n\t");
  dprintVector(chns);

  // Define background and signal processes
  map<string, VString> bkg_procs;
  VString bkgs, bkgs_em, sm_signals, main_sm_signals, mssm_ggH_signals, mssm_bbH_signals, mssm_signals;

  sm_signals = {"WH125", "ZH125", "ttH125"};
  main_sm_signals = {"ggH125", "qqH125"};

  mssm_ggH_signals = {"ggh_t", "ggh_b", "ggh_i", "ggH_t", "ggH_b", "ggH_i", "ggA_t", "ggA_b", "ggA_i"};
  mssm_bbH_signals = {"bbA", "bbH", "bbh"};
  if(analysis == "mssm" || analysis == "mssm_btag")
  {
    mssm_ggH_signals = {"ggh_t", "ggh_b", "ggh_i"};
    mssm_bbH_signals = {"bbh"};
  }
  mssm_signals = ch::JoinStr({mssm_ggH_signals, mssm_bbH_signals});

  bkgs = {"EMB", "ZL", "TTL", "VVL", "jetFakes", "ggHWW125", "qqHWW125", "WHWW125", "ZHWW125"};
  bkgs_em = {"EMB", "W", "QCD", "ZL", "TTL", "VVL", "ggHWW125", "qqHWW125", "WHWW125", "ZHWW125"};
  if (no_emb) {
    dout("WARNING: the EMB process is removed from backgrounds");
    bkgs.erase(std::remove(bkgs.begin(), bkgs.end(), "EMB"), bkgs.end());
    bkgs_em.erase(std::remove(bkgs_em.begin(), bkgs_em.end(), "EMB"), bkgs_em.end());
  }
  map<int, VString> SUSYggH_masses;
  SUSYggH_masses[2016] = {"110","120","130","140","160","180","200","250","300","350","400","450","500","600","700","800","900","1200","1400","1500","1600","1800","2000","2300","2600","2900","3200"};
  SUSYggH_masses[2017] = {"110","120","130","140","180","200","250","300","350","400","450","600","700","800","900","1200","1400","1500","1600","1800","2000","2300","2600","2900","3200"};
  SUSYggH_masses[2018] = {"110","120","130","140","160","180","200","250","300","350","400","450","600","700","800","900","1200","1400","1500","1600","1800","2000","2300","2600","2900","3200"};

  map<int, VString> SUSYbbH_masses;
  SUSYbbH_masses[2016] = {"110","120","130","140","160","180","200","250","350","400","450","500","600","700","800","900","1000","1200","1400","1600","1800","2000","2300","2600","2900","3200"};
  SUSYbbH_masses[2017] = {"110","120","125","130","140","160","180","200","250","300","350","400","450","500","600","700","800","900","1000","1200","1400","1600","1800","2000","2300","2600","2900","3200"};
  SUSYbbH_masses[2018] = {"110","120","125","130","140","160","180","200","250","300","350","400","450","500","600","700","800","900","1000","1200","1400","1600","1800","2000","2300","2600","2900","3200"};

  if (mass_susy_ggH.size() > 0)
  {
      doutnonl("WARNING: The masses for SUSY ggH are set manually:");
      SUSYggH_masses[era] = mass_susy_ggH;
      dprintVector(SUSYggH_masses[era]);
  }
  if (mass_susy_qqH.size() > 0)
  {
      doutnonl("WARNING: The masses for SUSY qqH are set manually:");
      SUSYbbH_masses[era] = mass_susy_qqH;
      dprintVector(SUSYbbH_masses[era]);
  }
  std::cout << "[INFO] Considering the following processes as main backgrounds:\n";

  if (chan.find("em") != std::string::npos || chan.find("all") != std::string::npos) {
    std::cout << "For em channel : \n\t";
    printVector(bkgs_em);
  }
  if (chan.find("mt") != std::string::npos || chan.find("et") != std::string::npos || chan.find("tt") != std::string::npos || chan.find("all") != std::string::npos) {
    std::cout << "For et,mt,tt channels : \n\t";
    printVector(bkgs);
  }
  bkg_procs["et"] = bkgs;
  bkg_procs["mt"] = bkgs;
  bkg_procs["tt"] = bkgs;
  bkg_procs["em"] = bkgs_em;

  if(analysis == "sm"){
    for(auto chn : chns){
        bkg_procs[chn] = JoinStr({bkg_procs[chn],sm_signals});
    }
  }
  else if(analysis == "mssm"){
    for(auto chn : chns){
        bkg_procs[chn] = JoinStr({bkg_procs[chn],sm_signals,main_sm_signals});
    }
  }

  // Define MSSM model-dependent mass parameters mA, mH, mh
  RooRealVar mA("mA", "mA", 125., 90., 4000.);
  RooRealVar mH("mH", "mH", 125., 90., 4000.);
  RooRealVar mh("mh", "mh", 125., 90., 4000.);
  mA.setConstant(true);

  // Define MSSM model-independent mass parameter MH
  RooRealVar MH("MH", "MH", 125., 90., 4000.);
  MH.setConstant(true);

  // Define categories
  map<string, Categories> cats;
  // STXS stage 0 categories (optimized on ggH and VBF)
  if(analysis == "mssm" || analysis == "mssm_vs_sm_classic"){
    cats["et"] = {
        {  1, "et_wjets_control"},
        { 32, "et_nobtag_tightmt"},
        { 33, "et_nobtag_loosemt"},
        { 35, "et_btag_tightmt"},
        { 36, "et_btag_loosemt"},
    };
    cats["mt"] = {
        {  1, "mt_wjets_control"},
        { 32, "mt_nobtag_tightmt"},
        { 33, "mt_nobtag_loosemt"},
        { 35, "mt_btag_tightmt"},
        { 36, "mt_btag_loosemt"},
    };
    cats["tt"] = {
        { 32, "tt_nobtag"},
        { 35, "tt_btag"},
    };
    cats["em"] = {
        {  1, "em_ttbar_control"},
        { 32, "em_nobtag_highdzeta"},
        { 33, "em_nobtag_mediumdzeta"},
        { 34, "em_nobtag_lowdzeta"},
        { 35, "em_btag_highdzeta"},
        { 36, "em_btag_mediumdzeta"},
        { 37, "em_btag_lowdzeta"},
    };
  }
  else if(analysis == "sm"){
    cats["et"] = {
        { 1, "et_wjets_control"},

        {10, "et_nobtag_lowmsv_0jet_tightmt"},
        {11, "et_nobtag_lowmsv_0jet_loosemt"},

        {12, "et_nobtag_lowmsv_geq1jet_highdeltar"},

        {13, "et_nobtag_lowmsv_1jet_lowdeltar_lowpt"},
        {14, "et_nobtag_lowmsv_1jet_lowdeltar_mediumpt"},
        {15, "et_nobtag_lowmsv_1jet_lowdeltar_highpt"},

        {16, "et_nobtag_lowmsv_2jet_lowdeltar_lowmjj"},
        {17, "et_nobtag_lowmsv_2jet_lowdeltar_mediummjj"},
        {18, "et_nobtag_lowmsv_2jet_lowdeltar_highmjj"},
    };
    cats["mt"] = {
        { 1, "mt_wjets_control"},

        {10, "mt_nobtag_lowmsv_0jet_tightmt"},
        {11, "mt_nobtag_lowmsv_0jet_loosemt"},

        {12, "mt_nobtag_lowmsv_geq1jet_highdeltar"},

        {13, "mt_nobtag_lowmsv_1jet_lowdeltar_lowpt"},
        {14, "mt_nobtag_lowmsv_1jet_lowdeltar_mediumpt"},
        {15, "mt_nobtag_lowmsv_1jet_lowdeltar_highpt"},

        {16, "mt_nobtag_lowmsv_2jet_lowdeltar_lowmjj"},
        {17, "mt_nobtag_lowmsv_2jet_lowdeltar_mediummjj"},
        {18, "mt_nobtag_lowmsv_2jet_lowdeltar_highmjj"},
    };
    cats["tt"] = {
        {10, "tt_nobtag_lowmsv_highdeltar"},

        {11, "tt_nobtag_lowmsv_0jet_lowmediumdeltar"},

        {12, "tt_nobtag_lowmsv_1jet_lowpt_lowdeltar"},
        {13, "tt_nobtag_lowmsv_1jet_lowpt_mediumdeltar"},
        {14, "tt_nobtag_lowmsv_1jet_highpt_lowmediumdeltar"},

        {15, "tt_nobtag_lowmsv_2jet_lowdeltar_lowmjj"},
        {16, "tt_nobtag_lowmsv_2jet_lowdeltar_highmjj_lowjdeta"},
        {17, "tt_nobtag_lowmsv_2jet_lowdeltar_highmjj_highjdeta"},
    };
    cats["em"] = {
        { 1, "em_ttbar_control"},

        {10, "em_nobtag_lowmsv_0jet_lowpt_mediumdzeta"},
        {11, "em_nobtag_lowmsv_0jet_lowpt_lowdzeta"},
        {12, "em_nobtag_lowmsv_0jet_highpt_mediumdzeta"},
        {13, "em_nobtag_lowmsv_0jet_highpt_lowdzeta"},

        {14, "em_nobtag_lowmsv_1jet_lowpt"},
        {15, "em_nobtag_lowmsv_1jet_lowmediumpt"},
        {16, "em_nobtag_lowmsv_1jet_highmediumpt"},
        {17, "em_nobtag_lowmsv_1jet_highpt"},

        {18, "em_nobtag_lowmsv_2jet_lowmjj"},
        {19, "em_nobtag_lowmsv_2jet_mediummjj"},
    };
  }
  else if(analysis == "mssm_vs_sm"){
    cats["et"] = {
        { 1, "et_wjets_control"},

        {10, "et_nobtag_lowmsv_0jet_tightmt"}, // No bbA
        {11, "et_nobtag_lowmsv_0jet_loosemt"},

        {12, "et_nobtag_lowmsv_geq1jet_highdeltar"},

        {13, "et_nobtag_lowmsv_1jet_lowdeltar_lowpt"},
        {14, "et_nobtag_lowmsv_1jet_lowdeltar_mediumpt"}, // No bbA
        {15, "et_nobtag_lowmsv_1jet_lowdeltar_highpt"}, // No bbA

        {16, "et_nobtag_lowmsv_2jet_lowdeltar_lowmjj"},
        {17, "et_nobtag_lowmsv_2jet_lowdeltar_mediummjj"},
        {18, "et_nobtag_lowmsv_2jet_lowdeltar_highmjj"}, // No bbA

        {32, "et_nobtag_highmsv_tightmt"},
        {33, "et_nobtag_highmsv_loosemt"},

        {35, "et_btag_tightmt"},
        {36, "et_btag_loosemt"},
    };
    cats["mt"] = {
        { 1, "mt_wjets_control"},

        {10, "mt_nobtag_lowmsv_0jet_tightmt"},
        {11, "mt_nobtag_lowmsv_0jet_loosemt"}, // No bbA

        {12, "mt_nobtag_lowmsv_geq1jet_highdeltar"},

        {13, "mt_nobtag_lowmsv_1jet_lowdeltar_lowpt"},
        {14, "mt_nobtag_lowmsv_1jet_lowdeltar_mediumpt"},
        {15, "mt_nobtag_lowmsv_1jet_lowdeltar_highpt"},

        {16, "mt_nobtag_lowmsv_2jet_lowdeltar_lowmjj"}, // No bbA
        {17, "mt_nobtag_lowmsv_2jet_lowdeltar_mediummjj"}, // No bbA
        {18, "mt_nobtag_lowmsv_2jet_lowdeltar_highmjj"}, // No bbA

        {32, "mt_nobtag_highmsv_tightmt"},
        {33, "mt_nobtag_highmsv_loosemt"},

        {35, "mt_btag_tightmt"},
        {36, "mt_btag_loosemt"},
    };
    cats["tt"] = {
        {10, "tt_nobtag_lowmsv_highdeltar"},

        {11, "tt_nobtag_lowmsv_0jet_lowmediumdeltar"}, // No bbA

        {12, "tt_nobtag_lowmsv_1jet_lowpt_lowdeltar"},
        {13, "tt_nobtag_lowmsv_1jet_lowpt_mediumdeltar"}, // No bbA
        {14, "tt_nobtag_lowmsv_1jet_highpt_lowmediumdeltar"}, // No bbA

        {15, "tt_nobtag_lowmsv_2jet_lowdeltar_lowmjj"},
        {16, "tt_nobtag_lowmsv_2jet_lowdeltar_highmjj_lowjdeta"}, // No bbA
        {17, "tt_nobtag_lowmsv_2jet_lowdeltar_highmjj_highjdeta"},

        {32, "tt_nobtag_highmsv"},

        {35, "tt_btag"},
    };
    cats["em"] = {
        { 1, "em_ttbar_control"},

        {10, "em_nobtag_lowmsv_0jet_lowpt_mediumdzeta"}, // No bbA
        {11, "em_nobtag_lowmsv_0jet_lowpt_lowdzeta"}, // No bbA, bbH
        {12, "em_nobtag_lowmsv_0jet_highpt_mediumdzeta"},
        {13, "em_nobtag_lowmsv_0jet_highpt_lowdzeta"},

        {14, "em_nobtag_lowmsv_1jet_lowpt"},
        {15, "em_nobtag_lowmsv_1jet_lowmediumpt"},
        {16, "em_nobtag_lowmsv_1jet_highmediumpt"}, // No bbA
        {17, "em_nobtag_lowmsv_1jet_highpt"}, // No bbA

        {18, "em_nobtag_lowmsv_2jet_lowmjj"},
        {19, "em_nobtag_lowmsv_2jet_mediummjj"},

        {32, "em_nobtag_highmsv_highdzeta"},
        {33, "em_nobtag_highmsv_mediumdzeta"},
        {34, "em_nobtag_highmsv_lowdzeta"}, // No bbA

        {35, "em_btag_highdzeta"},
        {36, "em_btag_mediumdzeta"},
        {37, "em_btag_lowdzeta"},
    };
  }
  else throw std::runtime_error("Given categorization is not known.");

  // Create combine harverster object
  ch::CombineHarvester cb;
  cb.SetFlag("workspaces-use-clone", true);


  // Introduce ordering of categories for the final discriminator in MSSM
  std::vector<int> sm_categories = {2, 3, 4, 5, 6, 7, 8, 9, 10,
                                    11,12,13,14,15,16,17,18,19,20,
                                    21,22,23,24,25,26,27,28,29,30,31}; // SM-like categories with m_sv as discriminator
  std::vector<int> mssm_categories = {32,33,34,35,36,37}; // MSSM-like categories with mt_tot as discriminator
  std::vector<int> control_region_categories = {1}; // control regions with mt_tot as discriminator

  for (auto chn : chns) {
    cb.AddObservations({"*"}, {"htt"}, {era_tag}, {chn}, cats[chn]);
    cb.AddProcesses({"*"}, {"htt"}, {era_tag}, {chn}, bkg_procs[chn], cats[chn], false);
    if(analysis == "sm"){
      cb.AddProcesses({""}, {"htt"}, {era_tag}, {chn}, main_sm_signals, cats[chn], true);
    }
    else if(analysis == "mssm" || analysis == "mssm_vs_sm_classic" || analysis == "mssm_vs_sm"){
      //if(analysis != "mssm_vs_sm")
      //{
        cb.AddProcesses(SUSYggH_masses[era], {"htt"}, {era_tag}, {chn}, mssm_ggH_signals, cats[chn], true);
        cb.AddProcesses(SUSYbbH_masses[era], {"htt"}, {era_tag}, {chn}, mssm_bbH_signals, cats[chn], true);
      //}
      //else
      //{
        //cb.AddProcesses(SUSYggH_masses[era], {"htt"}, {era_tag}, {chn}, {"ggh_i", "ggh_t", "ggh_b"}, cats[chn], true);
        //cb.AddProcesses(SUSYggH_masses[era], {"htt"}, {era_tag}, {chn}, {"ggH_i", "ggH_t", "ggH_b", "ggA_i", "ggA_t", "ggA_b"}, cats[chn], true);
        //cb.AddProcesses(SUSYbbH_masses[era], {"htt"}, {era_tag}, {chn}, {"bbh"}, cats[chn], true);
        //Categories highmass_cats = cats[chn];
        //std::vector<int> failed_cats;
        //if(chn == "em")      failed_cats = {10,11,16,17,34};
        //else if(chn == "et") failed_cats = {10,14,15,18};
        //else if(chn == "mt") failed_cats = {11,16,17,18};
        //else if(chn == "tt") failed_cats = {11,13,14,16};
        //for (auto catit = highmass_cats.begin(); catit != highmass_cats.end(); ++catit)
        //{
        //  //if(std::find(m_sv_categories.begin(), m_sv_categories.end(), (*catit).first) != m_sv_categories.end() ) highmass_cats.erase(catit);
        //  if(std::find(failed_cats.begin(), failed_cats.end(), (*catit).first) != failed_cats.end()){
        //    highmass_cats.erase(catit);
        //    --catit;
        //  }
        //}
        //cb.AddProcesses(SUSYbbH_masses[era], {"htt"}, {era_tag}, {chn}, {"bbH", "bbA"}, highmass_cats, true);
      //}
      if(analysis == "mssm_vs_sm_classic" || "mssm_vs_sm"){
        cb.AddProcesses({""}, {"htt"}, {era_tag}, {chn}, ch::JoinStr({main_sm_signals, sm_signals, {"qqh"}}), cats[chn], true);
      }
    }
  }

  // Add systematics
  ch::AddMSSMvsSMRun2Systematics(cb, true, true, true, true, true, era);

  // Define restriction to the desired category
  if(category != "all"){
    cb = cb.bin({category});
  }

  // Extract shapes from input ROOT files
  for (string chn : chns) {
    cb.cp().channel({chn}).backgrounds().ExtractShapes(
      input_dir[chn] + "htt_" + category + ".inputs-mssm-vs-sm-Run" + era_tag + "-" + variable + ".root", "$BIN/$PROCESS", "$BIN/$PROCESS_$SYSTEMATIC");
    if(analysis == "sm"){
      cb.cp().channel({chn}).process(main_sm_signals).ExtractShapes(
        input_dir[chn] + "htt_" + category + ".inputs-mssm-vs-sm-Run" + era_tag + "-" + variable + ".root", "$BIN/$PROCESS$MASS", "$BIN/$PROCESS$MASS_$SYSTEMATIC");
    }
    if(analysis == "mssm" || analysis == "mssm_vs_sm_classic" || analysis == "mssm_vs_sm"){
      cb.cp().channel({chn}).process(mssm_ggH_signals).ExtractShapes(
        input_dir[chn] + "htt_" + category + ".inputs-mssm-vs-sm-Run" + era_tag + "-" + variable + ".root", "$BIN/$PROCESS_$MASS", "$BIN/$PROCESS_$MASS_$SYSTEMATIC");
      cb.cp().channel({chn}).process(mssm_bbH_signals).ExtractShapes(
        input_dir[chn] + "htt_" + category + ".inputs-mssm-vs-sm-Run" + era_tag + "-" + variable + ".root", "$BIN/bbH_$MASS", "$BIN/bbH_$MASS_$SYSTEMATIC");
    }
    if(analysis == "mssm_vs_sm_classic" || analysis == "mssm_vs_sm"){
      cb.cp().channel({chn}).process(ch::JoinStr({sm_signals,main_sm_signals})).ExtractShapes(
        input_dir[chn] + "htt_" + category + ".inputs-mssm-vs-sm-Run" + era_tag + "-" + variable + ".root", "$BIN/$PROCESS$MASS", "$BIN/$PROCESS$MASS_$SYSTEMATIC");
      cb.cp().channel({chn}).process({"qqh"}).ExtractShapes(
        input_dir[chn] + "htt_" + category + ".inputs-mssm-vs-sm-Run" + era_tag + "-" + variable + ".root", "$BIN/qqH125$MASS", "$BIN/qqH125$MASS_$SYSTEMATIC");
    }
  }

  // Delete processes (other than mssm signals) with 0 yield
  cb.FilterProcs([&](ch::Process *p) {
    if (std::find(mssm_signals.begin(), mssm_signals.end(), p->process()) != mssm_signals.end())
    {
      return false;
    }
    bool null_yield = !(p->rate() > 0.0);
    if (null_yield) {
      std::cout << "[WARNING] Removing process with null yield: \n ";
      std::cout << ch::Process::PrintHeader << *p << "\n";
      cb.FilterSysts([&](ch::Systematic *s) {
        bool remove_syst = (MatchingProcess(*p, *s));
        return remove_syst;
      });
    }
    return null_yield;
  });


  // Modify systematic variations with yield <= 0
  cb.FilterSysts([&](ch::Systematic *s) {
    // For mssm signals: no action yet
    if (std::find(mssm_signals.begin(), mssm_signals.end(), s->process()) != mssm_signals.end())
    {
      return false;
    }
    // For remaining processes: Delete systematics since these result in a bogus norm error in combine for the remaining
    if (s->type() == "shape") {
      if (s->shape_u()->Integral() <= 0.0) {
        std::cout << "[WARNING] Removing systematic with null yield in up shift:" << std::endl;
        std::cout << ch::Systematic::PrintHeader << *s << "\n";
        return true;
      }
      if (s->shape_d()->Integral() <= 0.0) {
        std::cout << "[WARNING] Removing systematic with null yield in down shift:" << std::endl;
        std::cout << ch::Systematic::PrintHeader << *s << "\n";
        return true;
      }
    }
    return false;
  });

  // Special treatment for horizontally morphed mssm signals: Scale hists with negative intergral to zero, including its systematics
  std::cout << "[INFO] Setting mssm signals with negative yield to 0.\n";
  cb.ForEachProc([mssm_signals](ch::Process *p) {
    if (std::find(mssm_signals.begin(), mssm_signals.end(), p->process()) != mssm_signals.end())
    {
      if(p->rate() < 0.0){
        std::cout << "[WARNING] Setting mssm signal with negative yield to 0: \n ";
        std::cout << ch::Process::PrintHeader << *p << "\n";
        auto newhist = p->ClonedShape();
        newhist->Scale(0.0);
        p->set_shape(std::move(newhist), false);
      }
    }
  });

  cb.ForEachSyst([mssm_signals](ch::Systematic *s) {
    if (std::find(mssm_signals.begin(), mssm_signals.end(), s->process()) != mssm_signals.end())
    {
      if (s->type() == "shape") {
        if (s->shape_u()->Integral() <= 0.0 || s->shape_d()->Integral() <= 0.0) {
          std::cout << "[WARNING] Setting systematic for mssm signal with negative yield to 0: \n ";
          std::cout << ch::Systematic::PrintHeader << *s << "\n";
          auto newhist_u = s->ClonedShapeU();
          auto newhist_d = s->ClonedShapeD();
          newhist_u->Scale(0.0);
          newhist_d->Scale(0.0);
          s->set_shapes(std::move(newhist_u), std::move(newhist_d), nullptr);
        }
      }
    }
  });

  // At this point we can fix the negative bins for the remaining processes
  std::cout << "[INFO] Fixing negative bins.\n";
  cb.ForEachProc([](ch::Process *p) {
    if (ch::HasNegativeBins(p->shape())) {
      auto newhist = p->ClonedShape();
      ch::ZeroNegativeBins(newhist.get());
      p->set_shape(std::move(newhist), false);
    }
  });

  cb.ForEachSyst([](ch::Systematic *s) {
    if (s->type().find("shape") == std::string::npos)
      return;
    if (ch::HasNegativeBins(s->shape_u()) ||
        ch::HasNegativeBins(s->shape_d())) {
      auto newhist_u = s->ClonedShapeU();
      auto newhist_d = s->ClonedShapeD();
      ch::ZeroNegativeBins(newhist_u.get());
      ch::ZeroNegativeBins(newhist_d.get());
      s->set_shapes(std::move(newhist_u), std::move(newhist_d), nullptr);
    }
  });

  // Replacing observation with the sum of the backgrounds (Asimov data)
  // useful to be able to check this, so don't do the replacement
  // for these
  if (!real_data) {
    for (auto b : cb.cp().bin_set()) {
      std::cout << "[INFO] Replacing data with asimov in bin " << b << "\n";
      auto background_shape = cb.cp().bin({b}).backgrounds().GetShape();
      auto total_procs_shape = cb.cp().bin({b}).data().GetShape();
      total_procs_shape.Scale(0.0);
      // Desired Asimov model: BG + Higgs. Since ggH and qqH H->tautau treated as signal, remaining Higgs processes as BG. Need to access the signals() + bg shapes
      if(analysis == "sm"){
        auto signal_shape = cb.cp().bin({b}).signals().GetShape();
        bool no_signal = (signal_shape.GetNbinsX() == 1 && signal_shape.Integral() == 0.0);
        bool no_background = (background_shape.GetNbinsX() == 1 && background_shape.Integral() == 0.0);
        if(no_signal && no_background)
        {
          std::cout << "\t[WARNING] No signal and no background available in bin " << b << std::endl;
        }
        else if(no_background)
        {
          std::cout << "\t[WARNING] No background available in bin " << b << std::endl;
          total_procs_shape = total_procs_shape + signal_shape;
        }
        else if(no_signal)
        {
          std::cout << "\t[WARNING] No signal available in bin " << b << std::endl;
          total_procs_shape = total_procs_shape + background_shape;
        }
        else
        {
          total_procs_shape = total_procs_shape + background_shape + signal_shape;
        }
      }
      // Desired Asimov model: BG + Higgs. Since H->tautau treated all as background, so it is sufficient to consider the bg shape
      else if(analysis == "mssm"){
        bool no_background = (background_shape.GetNbinsX() == 1 && background_shape.Integral() == 0.0);
        if(no_background)
        {
          std::cout << "\t[WARNING] No background available in bin " << b << std::endl;
        }
        else
        {
          total_procs_shape = total_procs_shape + background_shape;
        }
      }
      // Desired Asimov model: BG + Higgs. Since H->tautau treated all as signal (together with mssm !!!), need to retrieve the SM H->tautau shapes & add it to the asimov dataset
      else if(analysis == "mssm_vs_sm_classic" || analysis == "mssm_vs_sm"){
        auto sm_signal_shape = cb.cp().bin({b}).process(ch::JoinStr({sm_signals, main_sm_signals})).GetShape();
        bool no_background = (background_shape.GetNbinsX() == 1 && background_shape.Integral() == 0.0);
        bool no_sm_signal = (sm_signal_shape.GetNbinsX() == 1 && sm_signal_shape.Integral() == 0.0);
        if(no_sm_signal && no_background)
        {
          std::cout << "\t[WARNING] No signal and no background available in bin " << b << std::endl;
        }
        else if(no_background)
        {
          std::cout << "\t[WARNING] No background available in bin " << b << std::endl;
          total_procs_shape = total_procs_shape + sm_signal_shape;
        }
        else if(no_sm_signal)
        {
          std::cout << "\t[WARNING] No signal available in bin " << b << std::endl;
          total_procs_shape = total_procs_shape + background_shape;
        }
        else
        {
          total_procs_shape = total_procs_shape + background_shape + sm_signal_shape;
        }
      }
      cb.cp().bin({b}).ForEachObs([&](ch::Observation *obs) {
        obs->set_shape(total_procs_shape,true);
      });
    }
  }

  // Perform auto-rebinning
  if (auto_rebin) {
    std::cout << "[INFO] Performing auto-rebinning.\n";
    auto rebin = ch::AutoRebin().SetBinThreshold(5.0).SetBinUncertFraction(0.9).SetRebinMode(1).SetPerformRebin(true).SetVerbosity(1);
    rebin.Rebin(cb, cb);
  }

  // This function modifies every entry to have a standardised bin name of
  // the form: {analysis}_{channel}_{bin_id}_{era}
  ch::SetStandardBinNames(cb, "$ANALYSIS_$CHANNEL_$BINID_$ERA");
  ch::CombineHarvester cb_obs = cb.deep().backgrounds();

  // Adding bin-by-bin uncertainties
  std::cout << "[INFO] Adding bin-by-bin uncertainties.\n";
  cb.SetAutoMCStats(cb, 0.0);

  // Setup morphed mssm signals for model-independent case
  RooWorkspace ws("htt", "htt");

  std::map<std::string, RooAbsReal *> mass_var = {
    {"ggh_t", &mh}, {"ggh_b", &mh}, {"ggh_i", &mh},
    {"ggH_t", &mH}, {"ggH_b", &mH}, {"ggH_i", &mH},
    {"ggA_t", &mA}, {"ggA_b", &mA}, {"ggA_i", &mA},
    {"bbh", &mh},
    {"bbH", &mH},
    {"bbA", &mA}
  };

  std::map<std::string, std::string> process_norm_map = {
    {"ggh_t", "prenorm"}, {"ggh_b", "prenorm"}, {"ggh_i", "prenorm"},
    {"ggH_t", "prenorm"}, {"ggH_b", "prenorm"}, {"ggH_i", "prenorm"},
    {"ggA_t", "prenorm"}, {"ggA_b", "prenorm"}, {"ggA_i", "prenorm"},
    {"bbh", "norm"},
    {"bbH", "norm"},
    {"bbA", "norm"}
  };

  if(analysis == "mssm")
  {
    mass_var = {
      {"ggh_t", &MH}, {"ggh_b", &MH}, {"ggh_i", &MH},
      {"bbh", &MH}
    };

    process_norm_map = {
      {"ggh_t", "prenorm"}, {"ggh_b", "prenorm"}, {"ggh_i", "prenorm"},
      {"bbh", "norm"}
    };

    std::cout << "[INFO] Adding aditional terms for mssm ggh NLO reweighting.\n";
    // Assuming sm fractions of t, b and i contributions of 'ggh' in model-independent analysis
    TFile fractions_sm(sm_gg_fractions.c_str());
    RooWorkspace *w_sm = (RooWorkspace*)fractions_sm.Get("w");
    w_sm->var("mh")->SetName("MH");
    RooAbsReal *t_frac = w_sm->function("ggh_t_SM_frac");
    RooAbsReal *b_frac = w_sm->function("ggh_b_SM_frac");
    RooAbsReal *i_frac = w_sm->function("ggh_i_SM_frac");
    t_frac->SetName("ggh_t_frac");
    b_frac->SetName("ggh_b_frac");
    i_frac->SetName("ggh_i_frac");
    ws.import(MH);
    ws.import(*t_frac, RooFit::RecycleConflictNodes());
    ws.import(*b_frac, RooFit::RecycleConflictNodes());
    ws.import(*i_frac, RooFit::RecycleConflictNodes());
    fractions_sm.Close();
  }


  if(analysis == "mssm" || analysis == "mssm_vs_sm_classic" || analysis == "mssm_vs_sm")
  {
    TFile morphing_demo(("htt_mssm_morphing_" + category+ "_"  + era_tag + "_" + analysis + "_demo.root").c_str(), "RECREATE");

    // Perform morphing
    auto mssm_signals = ch::JoinStr({mssm_ggH_signals,mssm_bbH_signals});
    std::cout << "[INFO] Performing template morphing for mssm ggh and bbh.\n";
    auto morphFactory = ch::CMSHistFuncFactory();
    morphFactory.SetHorizontalMorphingVariable(mass_var);
    morphFactory.Run(cb, ws, process_norm_map);

    if(analysis == "mssm"){
      // Adding 'norm' terms into workspace according to desired signals
      for (auto bin : cb.cp().bin_set())
      {
        for (auto proc : mssm_ggH_signals)
        {
          std::string prenorm_name = bin + "_" + proc + "_morph_prenorm";
          std::string norm_name = bin + "_" + proc + "_morph_norm";
          ws.factory(TString::Format("expr::%s('@0*@1',%s, %s_frac)", norm_name.c_str(), prenorm_name.c_str(), proc.c_str()));
        }
      }
    }

    // Saving workspace with morphed signals
    morphing_demo.cd();
    if (verbose)
      ws.Print();
    ws.Write();
    morphing_demo.Close();
    cb.AddWorkspace(ws);
    cb.ExtractPdfs(cb, "htt", "$BIN_$PROCESS_morph");
    cb.ExtractData("htt", "$BIN_data_obs");
    std::cout << "[INFO] Finished template morphing for mssm ggh and bbh.\n";
  }

  std::cout << "[INFO] Writing datacards to " << output_folder << std::endl;

  // Decide, how to write out the datacards depending on --category option
  if(category == "all") {
      // Write out datacards. Naming convention important for rest of workflow. We
      // make one directory per chn-cat, one per chn and cmb. In this code we only
      // store the individual datacards for each directory to be combined later.
      ch::CardWriter writer(output_folder + "/" + era_tag + "/$TAG/$BIN.txt",
                            output_folder + "/" + era_tag + "/$TAG/common/htt_input_" + era_tag + ".root");

      // We're not using mass as an identifier - which we need to tell the
      // CardWriter
      // otherwise it will see "*" as the mass value for every object and skip it
      writer.SetWildcardMasses({});

      // Set verbosity
      if (verbose)
        writer.SetVerbosity(1);

      // Write datacards combined and per channel
      writer.WriteCards("cmb", cb);

      for (auto chn : chns) {
        writer.WriteCards(chn, cb.cp().channel({chn}));
      }
  }
  else {
      // Write out datacards. Naming convention important for rest of workflow. We
      // make one directory per chn-cat, one per chn and cmb. In this code we only
      // store the individual datacards for each directory to be combined later.
      ch::CardWriter writer(output_folder + "/" + era_tag + "/$BIN/$BIN.txt",
                            output_folder + "/" + era_tag + "/$BIN/common/$BIN_input.root");

      // We're not using mass as an identifier - which we need to tell the
      // CardWriter
      // otherwise it will see "*" as the mass value for every object and skip it
      writer.SetWildcardMasses({});

      // Set verbosity
      if (verbose)
        writer.SetVerbosity(1);

      writer.WriteCards("", cb);

      ch::CardWriter writer_restore(output_folder + "/" + era_tag + "/restore_binning/$BIN/$BIN.txt",
                                    output_folder + "/" + era_tag + "/restore_binning/$BIN/common/$BIN_input.root");

      // We're not using mass as an identifier - which we need to tell the
      // CardWriter
      // otherwise it will see "*" as the mass value for every object and skip it
      writer_restore.SetWildcardMasses({});

      // Set verbosity
      if (verbose)
        writer_restore.SetVerbosity(1);

      writer_restore.WriteCards("", cb_obs);
  }

  if (verbose)
    cb.PrintAll();

  std::cout << "[INFO] Done producing datacards.\n";
}
