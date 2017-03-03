// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Strelka - Small Variant Caller
// Copyright (c) 2009-2016 Illumina, Inc.
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


#include "DenovoCallableProcessor.hh"
#include "pedicure_shared.hh"
#include "pedicure_streams.hh"

#include "starling_common/starling_pos_processor_base.hh"


///
///
struct pedicure_pos_processor : public starling_pos_processor_base
{
    typedef starling_pos_processor_base base_t;

    pedicure_pos_processor(
        const pedicure_options& opt,
        const pedicure_deriv_options& dopt,
        const reference_contig_segment& ref,
        const pedicure_streams& fileStreams);

    void reset() override;

    void
    resetRegion(
        const std::string& chromName,
        const known_pos_range2& reportRegion);

private:

    void
    process_pos_variants_impl(
        const pos_t pos,
        const bool isPosProceedingReportableRange) override;

    void
    process_pos_snp_denovo(const pos_t pos);

    void
    process_pos_indel_denovo(const pos_t pos);

    void
    aggregate_vcf(const std::string& chrom, const pos_t& pos, const std::string& vcf_line);

    /////////////////////////////

    // keep some of the original pedicure classes handy so we don't
    // have to down-cast:
    const pedicure_options& _opt;
    const pedicure_deriv_options& _dopt;
    const pedicure_streams& _streams;

    double _maxChromDepth = 0.;

    std::string prev_vcf_line="";
    pos_t prev_vcf_pos=-1;
    std::vector<std::pair<pos_t,std::string>> buffer;

    DenovoCallableProcessor _icallProcessor;

    std::vector<CleanedPileup> _tier2_cpi;
};
