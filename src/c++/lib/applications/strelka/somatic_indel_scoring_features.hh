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

#pragma once

#include "strelka_shared.hh"

#include "starling_common/AlleleReportInfo.hh"
#include "starling_common/starling_pos_processor_win_avg_set.hh"
#include "somatic_call_shared.hh"
#include "somatic_indel_grid.hh"
#include "strelka_vcf_locus_info.hh"
#include "SomaticIndelVcfWriter.hh"


/// Approximate indel AF from reads
///
double
calculateIndelAF(
    const AlleleSampleReportInfo& isri);

/// Approximate indel "other" frequency (OF) from reads
///
double
calculateIndelOF(
    const AlleleSampleReportInfo& isri);

/// Similar to
/// https://www.broadinstitute.org/gatk/gatkdocs/org_broadinstitute_gatk_tools_walkers_annotator_StrandOddsRatio.php
///
/// We adjust the counts for low coverage/low AF by adding 0.5 -- this deals with the
/// case where we don't actually observe reads on one strand.
///
double
calculateSOR(
    const AlleleSampleReportInfo& isri);

/// Calculate phred-scaled Fisher strand bias (p-value for the null hypothesis that
/// either REF or ALT counts are biased towards one particular strand)
///
double
calculateFS(const AlleleSampleReportInfo& isri);

/// Calculate the p-value using a binomial test for the null hypothesis that
/// the ALT allele occurs on a particular strand only.
///
double
calculateBSA(const AlleleSampleReportInfo& isri);

/// Calculate base-calling noise from window average set
///
double
calculateBCNoise(const win_avg_set& was);

/// Calculate AFR feature (log ratio between T_AF and N_AF)
///
double
calculateAlleleFrequencyRate(
    const AlleleSampleReportInfo& normalIndelSampleReportInfo,
    const AlleleSampleReportInfo& tumorIndelSampleReportInfo);

/// Calculate TNR feature (log ratio between T_AF and T_OF)
///
double
calculateTumorNoiseRate(const AlleleSampleReportInfo& tumorIndelSampleReportInfo);

/// Calculate LAR feature (log ratio between #alt reads in tumor and #ref reads in normal)
///
double
calculateLogAltRatio(const AlleleSampleReportInfo& nisri,
                     const AlleleSampleReportInfo& tisri);

/// Calculate LOR feature (log odds ratio for  T_REF T_ALT
///                                            N_REF N_ALT)
///
double
calculateLogOddsRatio(const AlleleSampleReportInfo& normalIndelSampleReportInfo,
                      const AlleleSampleReportInfo& tumorIndelSampleReportInfo);


/// Calculate empirical scoring features and add to smod
///
void
calculateScoringFeatures(
    const SomaticIndelVcfInfo& siInfo,
    const win_avg_set& n_was,
    const win_avg_set& t_was,
    const strelka_options& opt,
    const strelka_deriv_options& dopt,
    strelka_shared_modifiers_indel& smod);

