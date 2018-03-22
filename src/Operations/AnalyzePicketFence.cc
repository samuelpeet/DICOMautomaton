//AnalyzePicketFence.cc - A part of DICOMautomaton 2018. Written by hal clark.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>            //Needed for exit() calls.
#include <eigen3/Eigen/Dense>
#include <experimental/optional>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>    
#include <vector>

#include "../Insert_Contours.h"
#include "../Structs.h"
#include "AnalyzePicketFence.h"
#include "YgorImages.h"
#include "YgorMath.h"         //Needed for vec3 class.
#include "YgorMathPlottingGnuplot.h" //Needed for YgorMathPlottingGnuplot::*.
#include "YgorMisc.h"         //Needed for FUNCINFO, FUNCWARN, FUNCERR macros.
#include "YgorStats.h"        //Needed for Stats:: namespace.


std::list<OperationArgDoc> OpArgDocAnalyzePicketFence(void){
    std::list<OperationArgDoc> out;

    // This operation extracts MLC positions from a picket fence image.
    //
    // Note: This routine requires data to be pre-processed. The gross picket area should be isolated and the leaf
    //       junction areas contoured (one contour per junction). Both can be accomplished via thresholding.

    out.emplace_back();
    out.back().name = "ImageSelection";
    out.back().desc = "Images to operate on. Either 'none', 'last', 'first', or 'all'.";
    out.back().default_val = "last";
    out.back().expected = true;
    out.back().examples = { "none", "last", "first", "all" };
    
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
    out.back().name = "MLCModel";
    out.back().desc = "The MLC design geometry to use."
                      " 'VarianMillenniumMLC80' has 80 leafs in each bank;"
                      " leaves are 10mm wide at isocentre;"
                      " and the maximum static field size is 40cm x 40cm."
                      " 'VarianMillenniumMLC120' has 120 leafs in each bank;"
                      " the 40 central leaves are 5mm wide at isocentre;"
                      " the 20 peripheral leaves are 10mm wide;"
                      " and the maximum static field size is 40cm x 40cm."
                      " 'VarianHD120' has 120 leafs in each bank;"
                      " the 32 central leaves are 2.5mm wide at isocentre;"
                      " the 28 peripheral leaves are 5mm wide;"
                      " and the maximum static field size is 40cm x 22cm.";
    out.back().default_val = "VarianMillenniumMLC120";
    out.back().expected = true;
    out.back().examples = { "VarianMillenniumMLC80",
                            "VarianMillenniumMLC120",
                            "VarianHD120" };  // It would be nice to try auto-detect this...

    out.emplace_back();
    out.back().name = "MinimumJunctionSeparation";
    out.back().desc = "The minimum distance between junctions in DICOM units. This number is used to"
                      " de-duplicate automatically detected junctions. Analysis results should not be"
                      " sensitive to the specific value.";
    out.back().default_val = "10.0";
    out.back().expected = true;
    out.back().examples = { "5.0", "10.0", "15.0", "25.0" };

    return out;
}

Drover AnalyzePicketFence(Drover DICOM_data, OperationArgPkg OptArgs, std::map<std::string,std::string> /*InvocationMetadata*/, std::string /*FilenameLex*/){

    //---------------------------------------------- User Parameters --------------------------------------------------
    const auto ImageSelectionStr = OptArgs.getValueStr("ImageSelection").value();

    const auto ROILabelRegex = OptArgs.getValueStr("ROILabelRegex").value();
    const auto NormalizedROILabelRegex = OptArgs.getValueStr("NormalizedROILabelRegex").value();

    auto MLCModel = OptArgs.getValueStr("MLCModel").value();
    const auto MinimumJunctionSeparation = std::stol( OptArgs.getValueStr("MinimumJunctionSeparation").value() );

    //-----------------------------------------------------------------------------------------------------------------
    const auto roiregex = std::regex(ROILabelRegex, std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);

    const auto roinormalizedregex = std::regex(NormalizedROILabelRegex, std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);

    const auto regex_none  = std::regex("^no?n?e?$", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto regex_first = std::regex("^fi?r?s?t?$", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto regex_last  = std::regex("^la?s?t?$", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto regex_all   = std::regex("^al?l?$",   std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);

    const auto regex_VMLC80  = std::regex("^va?r?i?a?n?m?i?l?l?e?n?n?i?u?m?m?l?c?80$", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto regex_VMLC120 = std::regex("^va?r?i?a?n?mille?n?n?i?u?m?m?l?c?120$", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
    const auto regex_VHD120  = std::regex("^va?r?i?a?n?hd120$", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);


    if( !std::regex_match(ImageSelectionStr, regex_none)
    &&  !std::regex_match(ImageSelectionStr, regex_first)
    &&  !std::regex_match(ImageSelectionStr, regex_last)
    &&  !std::regex_match(ImageSelectionStr, regex_all) ){
        throw std::invalid_argument("Image selection is not valid. Cannot continue.");
    }

    //Stuff references to all contours into a list. Remember that you can still address specific contours through
    // the original holding containers (which are not modified here).
    std::list<std::reference_wrapper<contour_of_points<double>>> cop_all;
    for(auto & cc : DICOM_data.contour_data->ccs){
        for(auto & cop : cc.contours){
            cop_all.push_back( std::ref(cop) );
        }
    }

    //Whitelist contours using the provided regex.
    auto cop_ROIs = cop_all;
    cop_ROIs.remove_if([=](std::reference_wrapper<contour_of_points<double>> cop) -> bool {
                   const auto ROINameOpt = cop.get().GetMetadataValueAs<std::string>("ROIName");
                   const auto ROIName = ROINameOpt.value_or("");
                   return !(std::regex_match(ROIName,roiregex));
    });
    cop_ROIs.remove_if([=](std::reference_wrapper<contour_of_points<double>> cop) -> bool {
                   const auto ROINameOpt = cop.get().GetMetadataValueAs<std::string>("NormalizedROIName");
                   const auto ROIName = ROINameOpt.value_or("");
                   return !(std::regex_match(ROIName,roinormalizedregex));
    });
    const auto NumberOfJunctions = static_cast<long int>(cop_ROIs.size());

    if(NumberOfJunctions < 2) throw std::domain_error("At least 2 junctions are needed for this analysis.");


    auto iap_it = DICOM_data.image_data.begin();
    if(false){
    }else if(std::regex_match(ImageSelectionStr, regex_none)){ iap_it = DICOM_data.image_data.end();
    }else if(std::regex_match(ImageSelectionStr, regex_last)){
        if(!DICOM_data.image_data.empty()) iap_it = std::prev(DICOM_data.image_data.end());
    }
    while(iap_it != DICOM_data.image_data.end()){

        if((*iap_it)->imagecoll.images.empty()) throw std::invalid_argument("Unable to find an image to analyze.");

        planar_image<float, double> *animg = &( (*iap_it)->imagecoll.images.front() );

        // Using the contours that surround the leaf junctions, extract a 'long' and 'short' direction along the
        // junctions. This will orient the image and give us some orientation independence. We use principle component
        // analysis for this.
        //
        // Note: We can estimate these directions for each contour, but the analysis will be more consistent if all
        //       contours are considered simultaneously. The trade-off is that results may depend on the aspect ratio of
        //       the image. A zoomed-in picket fence may result in incorrect PCA estimation when using all contours
        //       simultaneously.
        //

        size_t N_verts = 0;
        for(auto &acop : cop_ROIs) N_verts += acop.get().points.size();
        Eigen::MatrixXd verts( N_verts, 3 );
        {
            size_t i = 0;
            for(auto &acop : cop_ROIs){
                const auto cent = acop.get().Centroid(); // We're interested in ROI shape, not ROI arrangement.
                for(auto &p : acop.get().points){
                    const auto c = (p - cent);
                    verts(i,0) = c.x;
                    verts(i,1) = c.y;
                    verts(i,2) = c.z;
                    ++i;
                }
            }
        }

        Eigen::MatrixXd centred = verts.rowwise() - verts.colwise().mean();
        Eigen::MatrixXd cov = centred.adjoint() * centred;
        Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eig(cov); // Solves the eigenproblem.

        Eigen::VectorXd long_dir = eig.eigenvectors().col(2); // Principle component eigenvector (long axis).
        Eigen::VectorXd short_dir = eig.eigenvectors().col(1); // (short axis).
        //Eigen::VectorXd ortho_dir = eig.eigenvectors().col(1); // (orthogonal out-of-plane direction).

        const auto long_unit  = (vec3<double>( long_dir(0),  long_dir(1),  long_dir(2)  )).unit();
        const auto short_unit = (vec3<double>( short_dir(0), short_dir(1), short_dir(2) )).unit();

        FUNCINFO("Junction-aligned direction is " << long_unit );
        FUNCINFO("Junction-orthogonal direction is " << short_unit );

        //Produce a pixel value profile-projection by ray-casting along the junction-orthogonal direction.
        const bool inhibit_sort = true;
        samples_1D<double> junction_ortho_projections;
        const auto corner_pos = animg->position(0, 0);

/* */
        //Use all pixels for profile generation.
        //
        // Note: these profiles are noisy. It might be better to only sample the region between junctions, which could
        //       also be cheaper than scanning every pixel.   TODO.
        for(long int row = 0; row < animg->rows; ++row){
            for(long int col = 0; col < animg->columns; ++col){
                const auto val = animg->value(row, col, 0);
                const auto rel_pos = (animg->position(row, col) - corner_pos);
                const auto long_line_L = rel_pos.Dot(long_unit);
                junction_ortho_projections.push_back( { long_line_L, 0.0, val, 0.0 }, inhibit_sort );
            }
        }
/* */        

        //Use pixels in-between junctions for profile generation.
        std::vector<vec3<double>> junction_centroids;
        for(auto &acop : cop_ROIs) junction_centroids.emplace_back( acop.get().Centroid() );

        //Remove duplicate junctions, which appear when the junction is piecemeal contoured (and is likely to happen).
        {
            std::sort(junction_centroids.begin(), junction_centroids.end(), [&](const vec3<double> &A, const vec3<double> &B) -> bool {
                const auto A_pos = A.Dot(short_unit);
                const auto B_pos = B.Dot(short_unit);
                return (A_pos < B_pos);
            });
            auto last = std::unique(junction_centroids.begin(), 
                                    junction_centroids.end(),
                                    [&](const vec3<double> &A, const vec3<double> &B) -> bool {
                                        const auto d = std::abs( (A - B).Dot(short_unit) );
                                        return (d < 0.5*MinimumJunctionSeparation);
                                    });
            junction_centroids.erase(last, junction_centroids.end());
        }

        //Add thin contours for inspecting the junction lines.
        DICOM_data.contour_data->ccs.emplace_back();
        for(const auto &cent : junction_centroids){
            Inject_Thin_Line_Contour(*animg,
                                     line<double>(cent, cent + long_unit),
                                     DICOM_data.contour_data->ccs.back(),
                                     animg->metadata);
        }

        std::vector<double> junction_cax_separations;
        for(size_t i = 0; i < junction_centroids.size(); ++i){
            const auto dR = (junction_centroids[i] - vec3<double>( 0.0, 0.0, 0.0 ));
            junction_cax_separations.push_back( dR.Dot(short_unit) );
        }
        std::sort(junction_cax_separations.begin(), junction_cax_separations.end());
        std::cout << "Junction-CAX separations: ";
        for(auto &d : junction_cax_separations) std::cout << d << "  ";
        std::cout << std::endl;

        std::vector<double> junction_separations;
        for(size_t i = 1; i < junction_centroids.size(); ++i){
            const auto dR = (junction_centroids[i] - junction_centroids[i-1]);
            junction_separations.push_back( std::abs( dR.Dot(short_unit) ) );
        }
        //std::sort(junction_separations.begin(), junction_separations.end());
        const auto min_junction_sep = Stats::Min(junction_separations);
        std::cout << "Minimum junction separation: " << min_junction_sep << std::endl;

/*
        //Used only for profile generation using pixels in-between junctions only.

        auto near_a_junction = [=](const vec3<double> &p) -> bool {
            //Determines if a given point is sufficiently far from the nearest junction.
            for(const auto &jc : junction_centroids){
                line<double> l(jc, jc + long_unit);
                const auto d = l.Distance_To_Point(p);
                if(d < 0.25 * min_junction_sep) return true;
            }
            return false;
        };
        auto between_junctions = [=](const vec3<double> &p) -> bool {
            //Determines if a given point is between at least two junctions.
            const auto d = (p - vec3<double>( 0.0, 0.0, 0.0 )).Dot(short_unit);
            return (  (junction_cax_separations.front() < d)
                   && (d < junction_cax_separations.back()) );
        };

        for(long int row = 0; row < animg->rows; ++row){
            for(long int col = 0; col < animg->columns; ++col){
                const auto pos = animg->position(row, col);
                //if(near_a_junction(pos) || !between_junctions(pos)) continue;
                if(!between_junctions(pos)) continue;

                const auto val = animg->value(row, col, 0);
                const auto rel_pos = (pos - corner_pos);
                const auto long_line_L = rel_pos.Dot(long_unit);
                junction_ortho_projections.push_back( { long_line_L, 0.0, val, 0.0 }, inhibit_sort );
            }
        }
*/        



        //Prepare the data to identify peaks in the junction-orthogonal direction to locate leaf pairs.
        junction_ortho_projections.stable_sort();
        const long int N_bins = static_cast<long int>( std::max(animg->rows, animg->columns));
        auto junction_ortho_profile = junction_ortho_projections.Aggregate_Equal_Sized_Bins_Weighted_Mean(N_bins);

        //auto junction_ortho_profile2 = junction_ortho_profile.Moving_Average_Two_Sided_Hendersons_23_point();
        //auto junction_ortho_profile2 = junction_ortho_profile.Moving_Median_Filter_Two_Sided_Equal_Weighting(2);
        //auto junction_ortho_profile2 = junction_ortho_profile.Moving_Average_Two_Sided_Gaussian_Weighting(4);
        auto junction_ortho_profile2 = junction_ortho_profile.Moving_Average_Two_Sided_Spencers_15_point();
        //bool l_OK;
        //auto junction_ortho_profile2 = NPRLL::Attempt_Auto_Smooth(junction_ortho_profile, &l_OK);
        //if(!l_OK) throw std::runtime_error("Unable to perform NPRLL to smooth junction-orthogonal profile.");

        //Perform a high-pass filter to remove some beam profile and imager bias dependence.
        junction_ortho_profile2 = junction_ortho_profile2.Subtract(
                                        junction_ortho_profile2.Moving_Average_Two_Sided_Gaussian_Weighting(15) );

        //Now find all (local) peaks via the derivative of the crossing points.
        //
        // Note: We find peaks (gaps between leaves) instead of troughs (middle of leaves) because the background (i.e.,
        //       dose behind the jaws) confounds trough isolation for leaves on the boundaries. Peaks are also sharper
        //       (i.e., more confined spatially owing to the smaller gap they arise from), whereas troughs can undulate
        //       considerably more.
        auto peaks = junction_ortho_profile2.Peaks();

        //Merge peaks that are separated by a small distance.
        //
        // Note: We are generous here because the leaf thickness is bounded to >2mm or so.
        peaks.Average_Coincident_Data(2.0);

        //Filter out spurious peaks that are not 'sharp' enough.
        //
        // Fit a line to the left-adjacent N datum.
        // Fit a line to the right-adjacent N datum.
        // Threshold on the smallest angle that separates them.

        if(peaks.size() < 5) std::runtime_error("Leaf-leakage peaks not correctly detected. Please verify input.");


        //Create lines for each detected leaf pair.
        std::vector<line<double>> leaf_lines;
        for(const auto &peak4 : peaks.samples){
            const auto cent = junction_centroids.front();
            const auto d = peak4[0]; // Projection of relative position onto the long-axis unit vector.

            const auto offset = (cent - corner_pos).Dot(long_unit);
            const auto R_peak = cent + long_unit * (d - offset); // Actual position of peak (on this junction).

            leaf_lines.emplace_back( R_peak, R_peak + short_unit );
        }

        //Auto-detect the MLC model, if possible.
        {
            auto StationName = animg->GetMetadataValueAs<std::string>("StationName");

            const auto regex_FVAREA2TB = std::regex(".*FVAREA2TB.*", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
            const auto regex_FVAREA4TB = std::regex(".*FVAREA4TB.*", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);
            const auto regex_FVAREA6TB = std::regex(".*FVAREA6TB.*", std::regex::icase | std::regex::nosubs | std::regex::optimize | std::regex::extended);

            if(false){
            }else if( std::regex_match(StationName.value(), regex_FVAREA2TB)
                  ||  std::regex_match(StationName.value(), regex_FVAREA4TB)
                  ||  std::regex_match(StationName.value(), regex_FVAREA6TB) ){
                MLCModel = "VarianMillenniumMLC120";
            }else{
                MLCModel = "VarianMillenniumMLC80";
            }
        }

        //Create lines for leaf pairs computed from knowledge about each machine's MLC geometry.
        {
            std::vector<double> mlc_offsets; // Closest point along leaf-pair line from CAX.

            //We have to project the MLC leaf pair positions according to the magnification at the image panel.
            auto RTImageSID = std::stod( animg->GetMetadataValueAs<std::string>("RTImageSID").value_or("1000.0") );
            auto magnify = [=](double CAX_offset){
                return CAX_offset * (RTImageSID / 1000.0);
            };

            if(false){
            }else if( std::regex_match(MLCModel, regex_VHD120) ){
                for(long int i = 0; i < 16; ++i){ // The middle 32 leaves.
                    const double x = (2.5 * i + 1.25);
                    const double X = magnify(x);
                    mlc_offsets.emplace_back( X );
                    mlc_offsets.emplace_back(-X );
                }
                for(long int i = 0; i < 14; ++i){ // The peripheral 28 leaves.
                    const double x = 40.0 + (5.0 * i + 2.5);
                    const double X = magnify(x);
                    mlc_offsets.emplace_back( X );
                    mlc_offsets.emplace_back(-X );
                }

            }else if( std::regex_match(MLCModel, regex_VMLC120) ){
                for(long int i = 0; i < 20; ++i){ // The middle 40 leaves.
                    const double x = (5.0 * i + 2.5);
                    const double X = magnify(x);
                    mlc_offsets.emplace_back( X );
                    mlc_offsets.emplace_back(-X );
                }
                for(long int i = 0; i < 10; ++i){ // The peripheral 20 leaves.
                    const double x = 100.0 + (10.0 * i + 5.0);
                    const double X = magnify(x);
                    mlc_offsets.emplace_back( X );
                    mlc_offsets.emplace_back(-X );
                }

            }else if( std::regex_match(MLCModel, regex_VMLC80) ){
                for(long int i = 0; i < 20; ++i){
                    const double x = (10.0 * i + 5.0);
                    const double X = magnify(x);
                    mlc_offsets.emplace_back( X );
                    mlc_offsets.emplace_back(-X );
                }

            }else{
                throw std::invalid_argument("MLC model not understood");
            }

            // Extract the long and short units from this angle...  TODO
            auto BeamLimitingDeviceAngle = animg->GetMetadataValueAs<std::string>("BeamLimitingDeviceAngle");
            auto rot_ang = std::stod( BeamLimitingDeviceAngle.value() ) * M_PI/180.0; // in radians.

            leaf_lines.clear();
            for(const auto &offset : mlc_offsets){
                const auto origin = vec3<double>(0.0, 0.0, 0.0);
                const auto pos = origin + (long_unit * offset);

                leaf_lines.emplace_back( pos, pos + short_unit );
            }
        }


        //Add thin contours for visually inspecting the location of the peaks.
        DICOM_data.contour_data->ccs.emplace_back();
        for(const auto &leaf_line : leaf_lines){
            try{ // Will throw if grossly out-of-bounds, but it's a pain to pre-filter -- ignore exceptions for now... TODO
                Inject_Thin_Line_Contour(*animg,
                                         leaf_line,
                                         DICOM_data.contour_data->ccs.back(),
                                         animg->metadata);
            }catch(const std::exception &){};
        }

        std::vector<double> peak_separations;
        for(size_t i = 1; i < peaks.samples.size(); ++i){
            const auto d = (peaks.samples[i][0] - peaks.samples[i-1][0]);
            peak_separations.push_back( std::abs( d ) );
        }
        //std::sort(peak_separations.begin(), peak_separations.end());
        const auto avg_peak_sep = Stats::Mean(peak_separations);
        std::cout << "Average peak separation: " << avg_peak_sep << std::endl;
        std::cout << "Median peak separation: " << Stats::Median(peak_separations) << std::endl;
        std::cout << "Peak separations: ";
        for(auto &d : peak_separations) std::cout << d << "  ";
        std::cout << std::endl;

 

        //YgorMathPlottingGnuplot::Shuttle<samples_1D<double>> plot_shtl_1(junction_ortho_profile, "Without smoothing");
        YgorMathPlottingGnuplot::Shuttle<samples_1D<double>> plot_shtl_2(junction_ortho_profile2, "Smoothed and filtered");
        YgorMathPlottingGnuplot::Shuttle<samples_1D<double>> plot_shtl_5(peaks, "Peaks");
        YgorMathPlottingGnuplot::Plot<double>({plot_shtl_2, plot_shtl_5}, "Junction-orthogonal profile", "DICOM position", "Average Pixel Intensity");

        
        // Loop control.
        ++iap_it;
        if(std::regex_match(ImageSelectionStr, regex_first)) break;
    }

    return DICOM_data;
}