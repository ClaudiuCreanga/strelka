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

#include "pedicure_pos_processor.hh"
#include "denovo_call.hh"
#include "denovo_indel_caller.hh"
#include "denovo_indel_call_vcf.hh"
#include "denovo_snv_caller.hh"
#include "denovo_snv_call_vcf.hh"

#include "blt_util/log.hh"
#include "starling_common/AlleleReportInfoUtil.hh"

#include <iomanip>
#include <sstream>


pedicure_pos_processor::
pedicure_pos_processor(
    const pedicure_options& opt,
    const pedicure_deriv_options& dopt,
    const reference_contig_segment& ref,
    const pedicure_streams& fileStreams)
    : base_t(opt,dopt,ref,fileStreams,opt.alignFileOpt.alignmentSampleInfo.size())
    , _opt(opt)
    , _dopt(dopt)
    , _streams(fileStreams)
    , _icallProcessor(fileStreams.denovo_callable_osptr())
    , _tier2_cpi(getSampleCount())
{
    using namespace PEDICURE_SAMPLETYPE;

    // setup indel buffer:
    {
        const unsigned sampleCount(getSampleCount());
        for (unsigned sampleIndex(0); sampleIndex<sampleCount; ++sampleIndex)
        {
            const bool isProband(_opt.alignFileOpt.alignmentSampleInfo.getSampleInfo(sampleIndex).stype == PROBAND);
            sample_info& sif(sample(sampleIndex));
            getIndelBuffer().registerSample(sif.estdepth_buff, sif.estdepth_buff_tier2, isProband);
        }

        getIndelBuffer().finalizeSamples();
    }
}



void
pedicure_pos_processor::
reset()
{
    base_t::reset();

    prev_vcf_line="";
    prev_vcf_pos=-1;
    buffer.clear();
}



void
pedicure_pos_processor::
resetRegion(
    const std::string& chromName,
    const known_pos_range2& reportRegion)
{
    base_t::resetRegionBase(chromName, reportRegion);

    // setup norm and max filtration depths
    {
        if (_opt.dfilter.is_depth_filter())
        {
            cdmap_t::const_iterator cdi(_dopt.dfilter.chrom_depth.find(chromName));
            if (cdi == _dopt.dfilter.chrom_depth.end())
            {
                std::ostringstream oss;
                oss << "ERROR: Can't find chromosome: '" << chromName << "' in chrom depth file: "
                    << _opt.dfilter.chrom_depth_file << "\n";
                throw blt_exception(oss.str().c_str());
            }
            _maxChromDepth = (cdi->second * _opt.dfilter.max_depth_factor);
        }
        assert(_maxChromDepth >= 0.);
    }

    // set indel buffer depth:
    {
        // get max proband depth
        double max_candidate_proband_sample_depth(-1.);
        {
            if (_dopt.dfilter.is_max_depth())
            {
                if (_opt.max_candidate_indel_depth_factor > 0.)
                {
                    max_candidate_proband_sample_depth = (_opt.max_candidate_indel_depth_factor * _maxChromDepth);
                }
            }

            if (_opt.max_candidate_indel_depth > 0.)
            {
                if (max_candidate_proband_sample_depth > 0.)
                {
                    max_candidate_proband_sample_depth = std::min(max_candidate_proband_sample_depth,static_cast<double>(_opt.max_candidate_indel_depth));
                }
                else
                {
                    max_candidate_proband_sample_depth = _opt.max_candidate_indel_depth;
                }
            }
        }

        getIndelBuffer().setMaxCandidateDepth(max_candidate_proband_sample_depth);
    }
}



void
pedicure_pos_processor::
process_pos_snp_denovo(const pos_t pos)
{
    using namespace PEDICURE_SAMPLETYPE;

    // skip site if proband depth is zero:
    {
        const unsigned probandIndex(_opt.alignFileOpt.alignmentSampleInfo.getTypeIndexList(PROBAND)[0]);
        const CleanedPileup& probandCpi(sample(probandIndex).cpi);

        // note this is a more expansive skipping criteria then we use for germline calling
        // (this is because there's no gvcf output)
        if (probandCpi.cleanedPileup().calls.empty()) return;
    }

    const unsigned sampleCount(getSampleCount());
    cpiPtrTiers_t pileups;
    pileups[PEDICURE_TIERS::TIER1].resize(sampleCount);
    pileups[PEDICURE_TIERS::TIER2].resize(sampleCount);
    {

        for (unsigned sampleIndex(0); sampleIndex<sampleCount; ++sampleIndex)
        {
            CleanedPileup* tier_cpi[] = { &(sample(sampleIndex).cpi), &(_tier2_cpi[sampleIndex])};

            for (unsigned tierIndex(0); tierIndex<PEDICURE_TIERS::SIZE; ++tierIndex)
            {
                pileups[tierIndex][sampleIndex] = tier_cpi[tierIndex];

                const bool is_include_tier2(tierIndex!=0);
                if (is_include_tier2 && (! _opt.tier2.is_tier2())) continue;
                sample_info& sif(sample(sampleIndex));
                _pileupCleaner.CleanPileup(sif.bc_buff.get_pos(pos),is_include_tier2,*(tier_cpi[tierIndex]));
            }
        }
    }

    const pos_t output_pos(pos+1);
    //const char ref_base(_ref.get_base(pos));

    const SampleInfoManager& sinfo(_opt.alignFileOpt.alignmentSampleInfo);
    denovo_snv_call dsc;
    dsc.is_forced_output = is_forced_output_pos(pos);

    get_denovo_snv_call(
        _opt,
        sinfo,
        pileups,
        dsc);

    if (_opt.is_denovo_callable())
    {
        _icallProcessor.addToRegion(_chromName, output_pos,
                                    sinfo, pileups[PEDICURE_TIERS::TIER1]);
    }

    // report events:

    dsc.consolidate_genotype();

    if (dsc.is_output())
    {
        std::stringstream bos;

        bos << _chromName<< '\t'
            << output_pos << '\t'
            << ".";
        denovo_snv_call_vcf(
            _opt,_dopt,
            sinfo,
            _maxChromDepth,
            pileups,
            dsc,
            bos);
        bos << "\n";

        aggregate_vcf(_chromName,output_pos,bos.str());
    }
}

void
pedicure_pos_processor::
process_pos_variants_impl(
    const pos_t pos,
    const bool isPosPrecedingReportableRange)
{
    if (isPosPrecedingReportableRange) return;

    try
    {
        process_pos_snp_denovo(pos);
    }
    catch (...)
    {
        log_os << "Exception caught while attempting to call denovo SNV at position: " << (pos+1) << "\n";
        throw;
    }

    try
    {
        process_pos_indel_denovo(pos);
    }
    catch (...)
    {
        log_os << "Exception caught while attempting to call denovo indel at position: " << (pos+1) << "\n";
        throw;
    }
}



void
pedicure_pos_processor::
process_pos_indel_denovo(const pos_t pos)
{
    using namespace PEDICURE_SAMPLETYPE;

    const SampleInfoManager& sinfo(_opt.alignFileOpt.alignmentSampleInfo);
    const unsigned sampleCount(getSampleCount());

    auto it(getIndelBuffer().positionIterator(pos));
    const auto it_end(getIndelBuffer().positionIterator(pos + 1));

    for (; it!=it_end; ++it)
    {
        const IndelKey& indelKey(it->first);

        // don't write breakpoint output:
        if (indelKey.is_breakpoint()) continue;

        const IndelData& indelData(getIndelData(it));

        if (!getIndelBuffer().isCandidateIndel(indelKey, indelData)) continue;

        // assert that indel data exists for all samples, make sure alt alignments are scored in at least one sample:
        bool isAllEmpty(true);
        for (unsigned sampleIndex(0); sampleIndex<sampleCount; ++sampleIndex)
        {
            if (! indelData.getSampleData(sampleIndex).read_path_lnp.empty()) isAllEmpty = false;
        }

        if (isAllEmpty) continue;

        std::string vcf_ref_seq, vcf_indel_seq;
        getSingleIndelAlleleVcfSummaryStrings(indelKey, indelData, _ref,  vcf_indel_seq, vcf_ref_seq);

        // STARKA-248 filter invalid indel. TODO: filter this issue earlier (occurs as, e.g. 1D1I which matches ref)
        if (vcf_indel_seq == vcf_ref_seq) continue;

        denovo_indel_call dindel;

        // considate sample_options:
        std::vector<const starling_sample_options*> sampleOptions(sampleCount);
        for (unsigned sampleIndex(0); sampleIndex<sampleCount; sampleIndex++)
        {
            sampleOptions[sampleIndex] = &(sample(sampleIndex).sample_opt);
        }

        static const bool is_use_alt_indel(true);
        get_denovo_indel_call(
            _opt,
            _dopt,
            sinfo,
            sampleOptions,
            indelKey,indelData,
            is_use_alt_indel,
            dindel);

        if (dindel.is_output())
        {
            // get sample specific info:
            std::vector<isriTiers_t> isri(sampleCount);
            for (unsigned tierIndex(0); tierIndex<PEDICURE_TIERS::SIZE; ++tierIndex)
            {
                const bool is_include_tier2(tierIndex==1);
                for (unsigned sampleIndex(0); sampleIndex<sampleCount; ++ sampleIndex)
                {
                    getAlleleSampleReportInfo(
                        _opt, _dopt, indelKey, indelData.getSampleData(sampleIndex), sample(sampleIndex).bc_buff,
                        is_include_tier2, is_use_alt_indel,
                        isri[sampleIndex][tierIndex]);
                }
            }

            pos_t indel_pos(indelKey.pos);
            if (indelKey.type != INDEL::BP_RIGHT)
            {
                indel_pos -= 1;
            }

            const pos_t output_pos(indel_pos+1);

            static const char sep('\t');

            std::stringstream bos;

            bos << _chromName << sep
                << output_pos << sep
                << ".";

            // REF/ALT
            bos << sep << vcf_ref_seq
                << sep << vcf_indel_seq;

            const AlleleReportInfo& indelReportInfo(indelData.getReportInfo());
            denovo_indel_call_vcf(_opt, _dopt, sinfo, _maxChromDepth, dindel, indelReportInfo, isri, bos);
            bos << "\n";

            aggregate_vcf(_chromName,output_pos,bos.str());
        }
    }
}



// needs replaced by a more comprehensive record integration
void
pedicure_pos_processor::
aggregate_vcf(const std::string& /*chrom*/, const pos_t& pos, const std::string& vcf_line)
{
    std::ostream& bos(*_streams.denovo_osptr());
//    std::ostream& bos(std::cout);

    // case in order
    if (prev_vcf_pos<pos)
    {
        prev_vcf_pos = pos;
        if (prev_vcf_pos>0)
            bos << prev_vcf_line;
        prev_vcf_line = vcf_line;
    }
    //case not in order
    else
    {
        bos << vcf_line;
    }
}
