//EvaluateNTCPModels.cc - A part of DICOMautomaton 2017. Written by hal clark.

#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <string>    
#include <vector>
#include <set> 
#include <map>
#include <unordered_map>
#include <list>
#include <functional>
#include <thread>
#include <array>
#include <mutex>
#include <limits>
#include <cmath>
#include <regex>

#include <getopt.h>           //Needed for 'getopts' argument parsing.
#include <cstdlib>            //Needed for exit() calls.
#include <utility>            //Needed for std::pair.
#include <algorithm>
#include <experimental/optional>

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>

#include <boost/interprocess/sync/named_mutex.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

#include "YgorMisc.h"         //Needed for FUNCINFO, FUNCWARN, FUNCERR macros.
#include "YgorMath.h"         //Needed for vec3 class.
#include "YgorMathPlottingGnuplot.h" //Needed for YgorMathPlottingGnuplot::*.
#include "YgorMathChebyshev.h" //Needed for cheby_approx class.
#include "YgorStats.h"        //Needed for Stats:: namespace.
#include "YgorFilesDirs.h"    //Needed for Does_File_Exist_And_Can_Be_Read(...), etc..
#include "YgorContainers.h"   //Needed for bimap class.
#include "YgorPerformance.h"  //Needed for YgorPerformance_dt_from_last().
#include "YgorAlgorithms.h"   //Needed for For_Each_In_Parallel<..>(...)
#include "YgorArguments.h"    //Needed for ArgumentHandler class.
#include "YgorString.h"       //Needed for GetFirstRegex(...)
#include "YgorImages.h"
#include "YgorImagesIO.h"
#include "YgorImagesPlotting.h"

#include "Explicator.h"       //Needed for Explicator class.

#include "../Structs.h"
#include "../BED_Conversion.h"
#include "../Contour_Collection_Estimates.h"

#include "../YgorImages_Functors/Grouping/Misc_Functors.h"

#include "../YgorImages_Functors/Processing/DCEMRI_AUC_Map.h"
#include "../YgorImages_Functors/Processing/DCEMRI_S0_Map.h"
#include "../YgorImages_Functors/Processing/DCEMRI_T1_Map.h"
#include "../YgorImages_Functors/Processing/Highlight_ROI_Voxels.h"
#include "../YgorImages_Functors/Processing/Kitchen_Sink_Analysis.h"
#include "../YgorImages_Functors/Processing/IVIMMRI_ADC_Map.h"
#include "../YgorImages_Functors/Processing/Time_Course_Slope_Map.h"
#include "../YgorImages_Functors/Processing/CT_Perfusion_Clip_Search.h"
#include "../YgorImages_Functors/Processing/CT_Perf_Pixel_Filter.h"
#include "../YgorImages_Functors/Processing/CT_Convert_NaNs_to_Air.h"
#include "../YgorImages_Functors/Processing/Min_Pixel_Value.h"
#include "../YgorImages_Functors/Processing/Max_Pixel_Value.h"
#include "../YgorImages_Functors/Processing/CT_Reasonable_HU_Window.h"
#include "../YgorImages_Functors/Processing/Slope_Difference.h"
#include "../YgorImages_Functors/Processing/Centralized_Moments.h"
#include "../YgorImages_Functors/Processing/Logarithmic_Pixel_Scale.h"
#include "../YgorImages_Functors/Processing/Per_ROI_Time_Courses.h"
#include "../YgorImages_Functors/Processing/DBSCAN_Time_Courses.h"
#include "../YgorImages_Functors/Processing/In_Image_Plane_Bilinear_Supersample.h"
#include "../YgorImages_Functors/Processing/In_Image_Plane_Bicubic_Supersample.h"
#include "../YgorImages_Functors/Processing/In_Image_Plane_Pixel_Decimate.h"
#include "../YgorImages_Functors/Processing/Cross_Second_Derivative.h"
#include "../YgorImages_Functors/Processing/Orthogonal_Slices.h"

#include "../YgorImages_Functors/Transform/DCEMRI_C_Map.h"
#include "../YgorImages_Functors/Transform/DCEMRI_Signal_Difference_C.h"
#include "../YgorImages_Functors/Transform/CT_Perfusion_Signal_Diff.h"
#include "../YgorImages_Functors/Transform/DCEMRI_S0_Map_v2.h"
#include "../YgorImages_Functors/Transform/DCEMRI_T1_Map_v2.h"
#include "../YgorImages_Functors/Transform/Pixel_Value_Histogram.h"
#include "../YgorImages_Functors/Transform/Subtract_Spatially_Overlapping_Images.h"

#include "../YgorImages_Functors/Compute/Per_ROI_Time_Courses.h"
#include "../YgorImages_Functors/Compute/Contour_Similarity.h"
#include "../YgorImages_Functors/Compute/AccumulatePixelDistributions.h"

#include "EvaluateNTCPModels.h"



std::list<OperationArgDoc> OpArgDocEvaluateNTCPModels(void){
    std::list<OperationArgDoc> out;


    out.emplace_back();
    out.back().name = "NTCPFileName";
    out.back().desc = "A filename (or full path) in which to append NTCP data generated by this routine."
                      " The format is CSV. Leave empty to dump to generate a unique temporary file.";
    out.back().default_val = "";
    out.back().expected = true;
    out.back().examples = { "", "/tmp/somefile", "localfile.csv", "derivative_data.csv" };
    out.back().mimetype = "text/csv";


    out.emplace_back();
    out.back().name = "NormalizedROILabelRegex";
    out.back().desc = "A regex matching ROI labels/names to consider. The default will match"
                      " all available ROIs. Be aware that input spaces are trimmed to a single space."
                      " If your ROI name has more than two sequential spaces, use regex to avoid them."
                      " All ROIs have to match the single regex, so use the 'or' token if needed."
                      " Regex is case insensitive and uses extended POSIX syntax.";
    out.back().default_val = ".*";
    out.back().expected = true;
    out.back().examples = { ".*", ".*Body.*", "Body", "Gross_Liver",
                            R"***(.*Left.*Parotid.*|.*Right.*Parotid.*|.*Eye.*)***",
                            R"***(Left Parotid|Right Parotid)***" };


    out.emplace_back();
    out.back().name = "ROILabelRegex";
    out.back().desc = "A regex matching ROI labels/names to consider. The default will match"
                      " all available ROIs. Be aware that input spaces are trimmed to a single space."
                      " If your ROI name has more than two sequential spaces, use regex to avoid them."
                      " All ROIs have to match the single regex, so use the 'or' token if needed."
                      " Regex is case insensitive and uses extended POSIX syntax.";
    out.back().default_val = ".*";
    out.back().expected = true;
    out.back().examples = { ".*", ".*body.*", "body", "Gross_Liver",
                            R"***(.*left.*parotid.*|.*right.*parotid.*|.*eyes.*)***",
                            R"***(left_parotid|right_parotid)***" };

    out.emplace_back();
    out.back().name = "NumberOfFractions";
    out.back().desc = "The number of fractions delivered."
                      " If needed, the dose distribution is converted to be biologically-equivalent to"
                      " a 2 Gy/fraction dose distribution.";
    out.back().default_val = "35";
    out.back().expected = true;
    out.back().examples = { "15", "25", "30", "35" };

    out.emplace_back();
    out.back().name = "AlphaBetaRatio";
    out.back().desc = "The ratio alpha/beta (in Gray) to use when converting to a biologically-equivalent"
                      " dose distribution. Default to 10 Gy for early-responding normal tissues and"
                      " tumors, and 3 Gy for late-responding normal tissues.";
    out.back().default_val = "3";
    out.back().expected = true;
    out.back().examples = { "1", "3", "10", "20" };

/*    
    out.emplace_back();
    out.back().name = "Gamma50";
    out.back().desc = "The unitless 'normalized dose-response gradient' or normalized slope of the logistic"
                      " dose-response model at the half-maximum point (e.g., D_50). Informally,"
                      " this parameter controls the steepness of the dose-response curve. (For more"
                      " specific information, consult a standard reference such as 'Basic Clinical Radiobiology'"
                      " 4th Edition by Joiner et al., sections 5.3-5.5.) This parameter is empirically"
                      " fit and not universal. Late endpoints for normal tissues have gamma_50 around 2-6"
                      " whereas gamma_50 nominally varies around 1.5-2.5 for local control of squamous"
                      " cell carcinomas of the head and neck.";
    out.back().default_val = "2.3";
    out.back().expected = true;
    out.back().examples = { "1.5", "2", "2.5", "6" };

    
    out.emplace_back();
    out.back().name = "Dose50";
    out.back().desc = "The dose (in Gray) needed to achieve 50\% probability of local tumour control according to"
                      " an empirical logistic dose-response model (e.g., D_50). Informally, this"
                      " parameter 'shifts' the model along the dose axis. (For more"
                      " specific information, consult a standard reference such as 'Basic Clinical Radiobiology'"
                      " 4th Edition by Joiner et al., sections 5.1-5.3.) This parameter is empirically"
                      " fit and not universal. In 'Quantifying the position and steepness of radiation "
                      " dose-response curves' by Bentzen and Tucker in 1994, D_50 of around 60-65 Gy are reported"
                      " for local control of head and neck cancers (pyriform sinus carcinoma and neck nodes with"
                      " max diameter <= 3cm). Martel et al. report 84.5 Gy in lung.";
    out.back().default_val = "65";
    out.back().expected = true;
    out.back().examples = { "37.9", "52", "60", "65", "84.5" };


    out.emplace_back();
    out.back().name = "EUD_Gamma50";
    out.back().desc = "The unitless 'normalized dose-response gradient' or normalized slope of the gEUD TCP model."
                      " It is defined only for the generalized Equivalent Uniform Dose (gEUD) model."
                      " This is sometimes referred to as the change in TCP for a unit change in dose straddled at"
                      " the TCD_50 dose. It is a counterpart to the Martel model's 'Gamma_50' parameter, but is"
                      " not quite the same."
                      " Okunieff et al. (doi:10.1016/0360-3016(94)00475-Z) computed Gamma50 for tumours in human"
                      " subjects across multiple institutions; they found a median of 0.8 for gross disease and"
                      " a median of 1.5 for microscopic disease. The inter-quartile range was [0.7:1.8] and"
                      " [0.7:2.2] respectively. (Refer to table 3 for site-specific values.) Additionally, "
                      " Gay et al. (doi:10.1016/j.ejmp.2007.07.001) claim that a value of 4.0 for late effects"
                      " a value of 2.0 for tumors in 'are reasonable initial estimates in [our] experience.' Their"
                      " table 2 lists (NTCP) estimates based on the work of Emami (doi:10.1016/0360-3016(91)90171-Y).";
    out.back().default_val = "0.8";
    out.back().expected = true;
    out.back().examples = { "0.8", "1.5" };
*/


    out.emplace_back();
    out.back().name = "LKB_TD50";
    out.back().desc = "The dose (in Gray) needed to deliver to the selected OAR that will induce the effect in 50\%"
                      " of cases.";
    out.back().default_val = "26.8";
    out.back().expected = true;
    out.back().examples = { "26.8" };


    out.emplace_back();
    out.back().name = "LKB_M";
    out.back().desc = "No description given...";
    out.back().default_val = "0.45";
    out.back().expected = true;
    out.back().examples = { "0.45" };


    out.emplace_back();
    out.back().name = "LKB_Alpha";
    out.back().desc = "The weighting factor \\alpha that controls the relative weighting of volume and dose"
                      " in the generalized Equivalent Uniform Dose (gEUD) model."
                      " When \\alpha=1, the gEUD is equivalent to the mean; when \\alpha=0, the gEUD is equivalent to"
                      " the geometric mean."
                      " Wu et al. (doi:10.1016/S0360-3016(01)02585-8) claim that for normal tissues, \\alpha can be"
                      " related to the Lyman-Kutcher-Burman (LKB) model volume parameter 'n' via \\alpha=1/n."
                      " Søvik et al. (doi:10.1016/j.ejmp.2007.09.001) found that gEUD is not strongly impacted by"
                      " errors in \\alpha. "
                      " Niemierko et al. ('A generalized concept of equivalent uniform dose. Med Phys 26:1100, 1999)"
                      " generated maximum likelihood estimates for 'several tumors and normal structures' which"
                      " ranged from -13.1 for local control of chordoma tumors to +17.7 for perforation of "
                      " esophagus." 
                      " Gay et al. (doi:10.1016/j.ejmp.2007.07.001) table 2 lists estimates based on the"
                      " work of Emami (doi:10.1016/0360-3016(91)90171-Y) for normal tissues ranging from 1-31."
                      " Brenner et al. (doi:10.1016/0360-3016(93)90189-3) recommend -7.2 for breast cancer, "
                      " -10 for melanoma, and -13 for squamous cell carcinomas. A 2017 presentation by Ontida "
                      " Apinorasethkul claims the tumour range spans [-40:-1] and the organs at risk range "
                      " spans [1:40]. AAPM TG report 166 also provides a listing of recommended values,"
                      " suggesting -10 for PTV and GTV, +1 for parotid, 20 for spinal cord, and 8-16 for"
                      " rectum, bladder, brainstem, chiasm, eye, and optic nerve. Burman (1991) and QUANTEC"
                      " (2010) also provide estimates.";
    out.back().default_val = "1.0";
    out.back().expected = true;
    out.back().examples = { "1", "3", "4", "20", "31" };


    out.emplace_back();
    out.back().name = "UserComment";
    out.back().desc = "A string that will be inserted into the output file which will simplify merging output"
                      " with differing parameters, from different sources, or using sub-selections of the data."
                      " If left empty, the column will be omitted from the output.";
    out.back().default_val = "";
    out.back().expected = true;
    out.back().examples = { "", "Using XYZ", "Patient treatment plan C" };

    return out;
}



Drover EvaluateNTCPModels(Drover DICOM_data, OperationArgPkg OptArgs, std::map<std::string,std::string> /*InvocationMetadata*/, std::string FilenameLex){

    // This operation evaluates a variety of NTCP models for each provided ROI. The selected ROI should be OARs.
    // Currently the following are implemented:
    //   - The LKB model.
    //   - The "Fenwick" model for solid tumours (in the lung; for a whole-lung OAR).
    //   - Forthcoming: modified Equivalent Uniform Dose (mEUD) NTCP model.
    //   - ...TODO...
    //
    // Note: This routine uses image_arrays so convert dose_arrays beforehand.
    //
    // Note: This routine will combine spatially-overlapping images by summing voxel intensities. So if you have a time
    //       course it may be more sensible to aggregate images in some way (e.g., spatial averaging) prior to calling
    //       this routine.
    //
    // Note: The LKB and mEUD both have their own gEUD 'alpha' parameter, but they are not necessarily shared.
    //       Huang et al. 2015 (doi:10.1038/srep18010) used alpha=1 for the LKB model and alpha=5 for the mEUD model.

    //---------------------------------------------- User Parameters --------------------------------------------------
    auto NTCPFileName = OptArgs.getValueStr("NTCPFileName").value();
    const auto ROILabelRegex = OptArgs.getValueStr("ROILabelRegex").value();
    const auto NormalizedROILabelRegex = OptArgs.getValueStr("NormalizedROILabelRegex").value();

    const auto LKB_M = std::stod( OptArgs.getValueStr("LKB_M").value() );
    const auto LKB_TD50 = std::stod( OptArgs.getValueStr("LKB_TD50").value() );
    const auto LKB_Alpha = std::stod( OptArgs.getValueStr("LKB_Alpha").value() );

    //const auto DosePerFraction = std::stod( OptArgs.getValueStr("DosePerFraction").value() );
    const auto AlphaBetaRatio = std::stod( OptArgs.getValueStr("AlphaBetaRatio").value() );
    const auto NumberOfFractions = std::stod( OptArgs.getValueStr("NumberOfFractions").value() );

    const auto UserComment = OptArgs.getValueStr("UserComment");

/*
    const auto Gamma50 = std::stod( OptArgs.getValueStr("Gamma50").value() );
    const auto Dose50 = std::stod( OptArgs.getValueStr("Dose50").value() );

    const auto EUD_Gamma50 = std::stod( OptArgs.getValueStr("EUD_Gamma50").value() );
    const auto EUD_TCD50 = std::stod( OptArgs.getValueStr("EUD_TCD50").value() );
    const auto EUD_Alpha = std::stod( OptArgs.getValueStr("EUD_Alpha").value() );
*/

    //-----------------------------------------------------------------------------------------------------------------
    const auto theregex = std::regex(ROILabelRegex, std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto thenormalizedregex = std::regex(NormalizedROILabelRegex, std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);

    Explicator X(FilenameLex);

    //Merge the image arrays if necessary.
    if(DICOM_data.image_data.empty()){
        throw std::invalid_argument("This routine requires at least one image array. Cannot continue");
    }

    auto img_arr_ptr = DICOM_data.image_data.front();
    if(img_arr_ptr == nullptr){
        throw std::runtime_error("Encountered a nullptr when expecting a valid Image_Array ptr.");
    }else if(img_arr_ptr->imagecoll.images.empty()){
        throw std::runtime_error("Encountered a Image_Array with valid images -- no images found.");
    }

    //Stuff references to all contours into a list. Remember that you can still address specific contours through
    // the original holding containers (which are not modified here).
    std::list<std::reference_wrapper<contour_collection<double>>> cc_all;
    for(auto & cc : DICOM_data.contour_data->ccs){
        auto base_ptr = reinterpret_cast<contour_collection<double> *>(&cc);
        cc_all.push_back( std::ref(*base_ptr) );
    }

    //Whitelist contours using the provided regex.
    auto cc_ROIs = cc_all;
    cc_ROIs.remove_if([=](std::reference_wrapper<contour_collection<double>> cc) -> bool {
                   const auto ROINameOpt = cc.get().contours.front().GetMetadataValueAs<std::string>("ROIName");
                   const auto ROIName = ROINameOpt.value();
                   return !(std::regex_match(ROIName,theregex));
    });
    cc_ROIs.remove_if([=](std::reference_wrapper<contour_collection<double>> cc) -> bool {
                   const auto ROINameOpt = cc.get().contours.front().GetMetadataValueAs<std::string>("NormalizedROIName");
                   const auto ROIName = ROINameOpt.value();
                   return !(std::regex_match(ROIName,thenormalizedregex));
    });

    if(cc_ROIs.empty()){
        throw std::invalid_argument("No contours selected. Cannot continue.");
    }

    std::string patient_ID;
    if( auto o = cc_ROIs.front().get().contours.front().GetMetadataValueAs<std::string>("PatientID") ){
        patient_ID = o.value();
    }else if( auto o = cc_ROIs.front().get().contours.front().GetMetadataValueAs<std::string>("StudyInstanceUID") ){
        patient_ID = o.value();
    }else{
        patient_ID = "unknown_patient";
    }

    //Accumulate the voxel intensity distributions.
    AccumulatePixelDistributionsUserData ud;
    if(!img_arr_ptr->imagecoll.Compute_Images( AccumulatePixelDistributions, { },
                                               cc_ROIs, &ud )){
        throw std::runtime_error("Unable to accumulate pixel distributions.");
    }

    //Evalute the models.
    std::map<std::string, double> LKBModel;
    std::map<std::string, double> FenwickModel;
//    std::map<std::string, double> mEUDModel;
    {
        for(const auto &av : ud.accumulated_voxels){
            const auto lROIname = av.first;

            const auto N = av.second.size();
            const long double V_frac = static_cast<long double>(1) / N; // Fractional volume of a single voxel compared to whole ROI.

            std::vector<double> LKB_gEUD_elements;
//            std::vector<double> mEUD_elements;
            LKB_gEUD_elements.reserve(N);
//            mEUD_elements.reserve(N);
            for(const auto &D_voxel : av.second){
                //Convert dose to BED or ED2 (aka "EQD2" -- the effective dose in 2 Gy fractions).
                const BEDabr BED_voxel = BEDabr_from_n_D_abr(NumberOfFractions, D_voxel, AlphaBetaRatio);
                const double ED2_voxel = D_from_d_BEDabr(2.0, BED_voxel);

                // LKB model.
                {
                    const auto scaled = std::pow(ED2_voxel, LKB_Alpha); //Problematic for (non-physical) 0.0.
                    if(std::isfinite(scaled)){
                        LKB_gEUD_elements.push_back(V_frac * std::pow(ED2_voxel, LKB_Alpha));
                    }else{
                        LKB_gEUD_elements.push_back(0.0); //No real need to include this except acknowledging what we are doing.
                    }
                }
                
                // mEUD model.
                {
//NOTE: this model only uses the 100c with the highest dose. So sort and filter the voxels before computing mEUD!
// Also, the model presented by Huang et al. is underspecified in their paper. Check the original for more comprehensive
// explanation.
//                    gEUD_elements.push_back(V_frac * std::pow(ED2_voxel, gEUD_Alpha));
                }

                // ... other models ...
                // ...

            }

            //Post-processing.
            {
                const auto OAR_mean_dose = Stats::Mean(av.second);
                const auto numer = OAR_mean_dose - 29.2;
                const auto denom = 13.1 * std::sqrt(2);
                const auto t = numer/denom;
                const auto NTCP_Fenwick = 0.5*(1.0 + std::erf(t));
                FenwickModel[lROIname] = NTCP_Fenwick;
            }
            {
                const long double LKB_gEUD = std::pow( Stats::Sum(LKB_gEUD_elements), static_cast<long double>(1) / LKB_Alpha );

                const long double numer = LKB_gEUD - LKB_TD50;
                const long double denom = LKB_M * LKB_TD50 * std::sqrt(2.0);
                const long double t = numer/denom;
                const long double NTCP_LKB = 0.5*(1.0 + std::erf(t));
                LKBModel[lROIname] = NTCP_LKB;
            }
            {
/*
                const long double mEUD = std::pow( Stats::Sum(mEUD_elements), static_cast<long double>(1) / EUD_Alpha );

                const long double numer = std::pow(mEUD, EUD_Gamma50*4);
                const long double denom = numer + std::pow(EUD_TCD50, EUD_Gamma50*4);
                double NTCP_mEUD = numer/denom; // This is a sigmoid curve.

                mEUDModel[lROIname] = NTCP_mEUD; 
*/
            }
        }
    }


    //Report the findings. 
    FUNCINFO("Attempting to claim a mutex");
    try{
        //File-based locking is used so this program can be run over many patients concurrently.
        //
        //Try open a named mutex. Probably created in /dev/shm/ if you need to clear it manually...
        boost::interprocess::named_mutex mutex(boost::interprocess::open_or_create,
                                               "dicomautomaton_operation_evaluatentcp_mutex");
        boost::interprocess::scoped_lock<boost::interprocess::named_mutex> lock(mutex);

        if(NTCPFileName.empty()){
            NTCPFileName = Get_Unique_Sequential_Filename("/tmp/dicomautomaton_evaluatentcp_", 6, ".csv");
        }
        const auto FirstWrite = !Does_File_Exist_And_Can_Be_Read(NTCPFileName);
        std::fstream FO_tcp(NTCPFileName, std::fstream::out | std::fstream::app);
        if(!FO_tcp){
            throw std::runtime_error("Unable to open file for reporting derivative data. Cannot continue.");
        }
        if(FirstWrite){ // Write a CSV header.
            FO_tcp << "UserComment,"
                   << "PatientID,"
                   << "ROIname,"
                   << "NormalizedROIname,"
                   << "NTCPLKBModel,"
//                   << "NTCPmEUDModel,"
                   << "NTCPFenwickModel,"
                   << "DoseMin,"
                   << "DoseMean,"
                   << "DoseMedian,"
                   << "DoseMax,"
                   << "DoseStdDev,"
                   << "VoxelCount"
                   << std::endl;
        }
        for(const auto &av : ud.accumulated_voxels){
            const auto lROIname = av.first;
            const auto DoseMin = Stats::Min( av.second );
            const auto DoseMean = Stats::Mean( av.second );
            const auto DoseMedian = Stats::Median( av.second );
            const auto DoseMax = Stats::Max( av.second );
            const auto DoseStdDev = std::sqrt(Stats::Unbiased_Var_Est( av.second ));
            const auto NTCPLKB = LKBModel[lROIname];
//            const auto NTCPmEUD = mEUDModel[lROIname];
            const auto NTCPFenwick = FenwickModel[lROIname];

            FO_tcp  << UserComment.value_or("") << ","
                    << patient_ID        << ","
                    << X(lROIname)       << ","
                    << lROIname          << ","
                    << NTCPLKB*100.0     << ","
//                    << NTCPmEUD*100.0    << ","
                    << NTCPFenwick*100.0 << ","
                    << DoseMin           << ","
                    << DoseMean          << ","
                    << DoseMedian        << ","
                    << DoseMax           << ","
                    << DoseStdDev        << ","
                    << av.second.size()
                    << std::endl;
        }
        FO_tcp.flush();
        FO_tcp.close();

    }catch(const std::exception &e){
        FUNCERR("Unable to write to output NTCP file: '" << e.what() << "'");
    }

    return std::move(DICOM_data);
}
