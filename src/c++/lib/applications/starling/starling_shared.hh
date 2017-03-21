//
// Strelka - Small Variant Caller
// Copyright (c) 2009-2017 Illumina, Inc.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//

///
/// \author Chris Saunders
///

#pragma once

#include "gvcf_options.hh"
#include "options/AlignmentFileOptions.hh"
#include "starling_common/starling_base_shared.hh"


struct starling_options : public starling_base_options
{
    starling_options()
    {
        bsnp_ssd_no_mismatch = 0.35;
        bsnp_ssd_one_mismatch = 0.6;
        max_win_mismatch = 2;
        max_win_mismatch_flank_size = 20;
        is_min_vexp = true;
        min_vexp = 0.25;

        // In practice we find that simple binomial error rates need to be heuristically scaled up in the germline model for good performance
        // This scaling is not used for (1) indel candidate selection (2) somatic calling, or any other models outside of diploid genotyping
        isIndelErrorRateFactor = true;
        indelErrorRateFactor = 100.;

        // In practice, we find that increasing the indel -> ref error rate relative to baseline improves performance
        // Its use cases are similar to those of indelErrorRateFactor
        isIndelRefErrorFactor = true;
        indelRefErrorFactor = 2.;

        // turn on short haplotyping
        is_short_haplotyping_enabled = true;
    }

    bool
    is_bsnp_diploid() const override
    {
        return is_ploidy_prior;
    }

    bool
    is_all_sites() const override
    {
        return true;
    }

    bool
    is_compute_germline_scoring_metrics() const override
    {
        return (isReportEVSFeatures || (! snv_scoring_model_filename.empty()) || (! indel_scoring_model_filename.empty()));
    }

    AlignmentFileOptions alignFileOpt;

    // empirical scoring models
    std::string snv_scoring_model_filename;
    std::string indel_scoring_model_filename;

    // Apply variant phasing:
    bool isUseVariantPhaser = false;

    // apply special behaviors for RNA-Seq analysis
    bool isRNA = false;

    gvcf_options gvcf;
};


/// data deterministically derived from the input options:
///
struct starling_deriv_options : public starling_base_deriv_options
{
    typedef starling_base_deriv_options base_t;

    explicit
    starling_deriv_options(const starling_options& opt)
        : base_t(opt),
          gvcf(opt.gvcf, opt.isRNA)
    {}

    gvcf_deriv_options gvcf;
};
