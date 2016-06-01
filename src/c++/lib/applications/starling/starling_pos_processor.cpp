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

#include "starling_pos_processor.hh"
#include "blt_common/position_nonref_test.hh"
#include "blt_common/position_nonref_2allele_test.hh"
#include "blt_common/ref_context.hh"
#include "blt_util/log.hh"
#include "starling_continuous_variant_caller.hh"

#include <iomanip>



static
void
report_counts(
    const snp_pos_info& pi,
    const unsigned n_unused_calls,
    const pos_t output_pos,
    std::ostream& os)
{
    unsigned base_count[N_BASE];

    for (unsigned i(0); i<N_BASE; ++i) base_count[i] = 0;

    for (const auto& bc : pi.calls)
    {
        assert(bc.base_id!=BASE_ID::ANY);
        base_count[bc.base_id]++;
    }

    os << output_pos << '\t';
    for (unsigned i(0); i<N_BASE; ++i)
    {
        os << base_count[i] << '\t';
    }
    os << n_unused_calls << '\n';
}



static
void
write_snp_prefix_info_file(const std::string& seq_name,
                           const pos_t output_pos,
                           const char ref,
                           const unsigned n_used_calls,
                           const unsigned n_unused_calls,
                           std::ostream& os)
{
    os << seq_name << "\t"
       << output_pos << "\t"
       << n_used_calls << "\t"
       << n_unused_calls << "\t"
       << ref;
}



starling_pos_processor::
starling_pos_processor(
    const starling_options& opt,
    const starling_deriv_options& dopt,
    const reference_contig_segment& ref,
    const starling_streams& streams)
    : base_t(opt,dopt,ref,streams,1),
      _opt(opt),
      _dopt(dopt),
      _streams(streams)
{
    static const unsigned sampleId(0);

    // setup gvcf aggregator
    if (_opt.gvcf.is_gvcf_output())
    {
        _gvcfer.reset(new gvcf_aggregator(
                          _opt,_dopt,ref,_nocompress_regions,
                          _streams.getSampleName(), _streams.gvcf_osptr(),
                          sample(sampleId).bc_buff));
    }

    // setup indel syncronizer:
    {
        sample_info& normal_sif(sample(0));

        double max_candidate_normal_sample_depth(-1.);
        if (dopt.gvcf.is_max_depth())
        {
            if (opt.max_candidate_indel_depth_factor > 0.)
            {
                max_candidate_normal_sample_depth = (opt.max_candidate_indel_depth_factor * dopt.gvcf.max_depth);
            }
        }

        if (opt.max_candidate_indel_depth > 0.)
        {
            if (max_candidate_normal_sample_depth > 0.)
            {
                max_candidate_normal_sample_depth = std::min(max_candidate_normal_sample_depth,static_cast<double>(opt.max_candidate_indel_depth));
            }
            else
            {
                max_candidate_normal_sample_depth = opt.max_candidate_indel_depth;
            }
        }

        const unsigned syncSampleId = getIndelBuffer().registerSample(normal_sif.estdepth_buff, normal_sif.estdepth_buff_tier2,
                                                       max_candidate_normal_sample_depth);

        assert(syncSampleId == sampleId);

        getIndelBuffer().finalizeSamples();
    }
}



void
starling_pos_processor::
insert_nocompress_region(
    const known_pos_range2& range)
{
    _stageman.validate_new_pos_value(range.begin_pos(),STAGE::READ_BUFFER);
    _nocompress_regions.addRegion(range);
    _is_skip_process_pos=false;
}



void
starling_pos_processor::
reset()
{
    base_t::reset();

    if (_opt.gvcf.is_gvcf_output())
    {
        _gvcfer->reset();
    }
}



void
starling_pos_processor::
process_pos_snp_single_sample(
    const pos_t pos,
    const unsigned sample_no)
{
    try
    {
        if (_opt.is_bsnp_diploid())
        {
            process_pos_snp_single_sample_impl(pos,sample_no);
        }
        else
        {
            process_pos_snp_single_sample_continuous(pos,sample_no);
        }

    }
    catch (...)
    {
        log_os << "Exception caught in starling_pos_processor_base.process_pos_snp_single_sample_impl() while processing chromosome position: " << (pos+1) << "\n"
               << "snp_pos_info:\n";
        log_os << sample(sample_no).bc_buff.get_pos(pos) << "\n";
        throw;
    }
}

void
starling_pos_processor::
process_pos_snp_single_sample_continuous(
    const pos_t pos,
    const unsigned sample_no)
{
    if (sample_no!=0) return;

    sample_info& sif(sample(sample_no));

    const CleanedPileup& cpi(sif.cpi);
    const snp_pos_info& pi(cpi.rawPileup());

    _pileupCleaner.CleanPileupErrorProb(sif.cpi);

    const snp_pos_info& good_pi(cpi.cleanedPileup());
    const bool is_forced(is_forced_output_pos(pos));

    if (pi.calls.empty() && !is_forced) return;

    std::unique_ptr<GermlineSiteCallInfo> si(new GermlineContinuousSiteCallInfo(pos,pi.get_ref_base(),good_pi,
                                                                                _opt.used_allele_count_min_qscore, _opt.min_het_vf, is_forced));

    si->n_used_calls=cpi.n_used_calls();
    si->n_unused_calls=cpi.n_unused_calls();
    // hpol filter
    si->hpol=get_snp_hpol_size(pos,_ref);


    starling_continuous_variant_caller::position_snp_call_continuous(_opt, good_pi, (GermlineContinuousSiteCallInfo&)*si);

    if (_opt.is_counts)
    {
        report_counts(good_pi,si->n_unused_calls,si->pos+1,*_streams.counts_osptr());
    }
    if (si->is_snp())
    {
        // this needs to be updated no matter where the snp-call is written to:
        if (_is_variant_windows)
        {
            _variant_print_pos.insert(pos);
            _is_skip_process_pos=false;
        }
    }

    if (_opt.gvcf.is_gvcf_output())
    {
        _gvcfer->add_site(std::move(si));
    }
}



void
starling_pos_processor::
process_pos_snp_single_sample_impl(
    const pos_t pos,
    const unsigned sample_no)
{
    // TODO:
    //
    // note this might not matter wrt larger changes taking place, but here goes:
    //
    // change filters to support vcf concept of 1..N filters which are added to the genotype information
    //
    // generalize site tests with an object
    //
    // genotype_test {
    //    ctor(); // setup any cached values
    //
    //    test(site_info);
    //
    //    write()?? (do we need to even bother with this?)
    // }
    //

    sample_info& sif(sample(sample_no));

    const CleanedPileup& cpi(sif.cpi);
    const snp_pos_info& pi(cpi.rawPileup());

    // note multi-sample status -- can still be called only for one sample
    // and only for sample 0. working on generalization:
    //
    if (sample_no!=0) return;

    const bool is_forced(is_forced_output_pos(pos));

    // the second term in is_skippable below forces sites to go through the pipeline
    // if phaser has put a hold on buffer cleanup. This ensures that the phaser will be turned back off
    //
    // TODO: there must be a way to force correct usage into the phaser's API instead of requiring this brittle hack
    const bool is_skippable(! (is_forced || is_save_pileup_buffer()));

    if (pi.calls.empty() && is_skippable) return;

    _pileupCleaner.CleanPileupErrorProb(sif.cpi);

    const snp_pos_info& good_pi(cpi.cleanedPileup());
    const extended_pos_info& good_epi(cpi.getExtendedPosInfo());


    std::unique_ptr<GermlineDiploidSiteCallInfo> si(new GermlineDiploidSiteCallInfo(pos,pi.get_ref_base(),good_pi,_opt.used_allele_count_min_qscore, is_forced));
    si->n_used_calls=cpi.n_used_calls();
    si->n_unused_calls=cpi.n_unused_calls();


    // delay writing any snpcalls so that anomaly tests can (optionally) be applied as filters:
    //
    nonref_test_call nrc;
    //lrt_snp_call lsc;
    //monoploid_genotype mgt;
    //std::unique_ptr<nploid_genotype> ngt_ptr;

    // check whether we're in a haploid region:
    si->dgt.ploidy=(get_ploidy(pos));

    const pos_t output_pos(pos+1);

    if (_opt.is_counts)
    {
        report_counts(good_pi,si->n_unused_calls,output_pos,*_streams.counts_osptr());
    }

    if (_opt.is_nonref_test() || _opt.is_nonref_sites())
    {
        position_nonref_2allele_test(good_pi,
                                     _opt,
                                     _opt.is_nonref_sites(),
                                     nrc);
#if 0
        static const bool is_mle_freq(false);

        position_nonref_test(good_pi,
                             _opt.nonref_variant_rate,
                             _opt.min_nonref_freq,
                             is_mle_freq,
                             nrc);
#endif

    }

#if 0
    if (_opt.is_lsnp)
    {
        position_snp_call_lrt(_opt.lsnp_alpha,good_pi,lsc);
    }
#endif
    if (_opt.is_bsnp_diploid())
    {
        _dopt.pdcaller().position_snp_call_pprob_digt(
            _opt,good_epi,si->dgt, _opt.is_all_sites());
    }
#if 0
    if (_opt.is_bsnp_monoploid)
    {
        position_snp_call_pprob_monogt(_opt.bsnp_monoploid_theta,good_pi,mgt);
    }
    if (_opt.is_bsnp_nploid)
    {
        ngt_ptr.reset(new nploid_genotype(*_ninfo));
        position_snp_call_pprob_nploid(_opt.bsnp_nploid_snp_prob,good_pi,*_ninfo,*ngt_ptr);
    }
#endif

    //    const bool is_snp(nrc.is_snp || lsc.is_snp || _site_info.dgt.is_snp || mgt.is_snp || (ngt_ptr.get() && ngt_ptr->is_snp));
    const bool is_snp(nrc.is_snp || si->dgt.is_snp);

    // find anomalies:
    //
#if 0
    bool is_pos_adis(false);
    bool is_pos_acov(false);

    if ((_opt.is_adis_table || _opt.is_adis_lrt) && is_snp)
    {
        if (_opt.is_adis_table)
        {
            is_pos_adis = (is_pos_adis || position_strand_distro_anomaly(_opt.adis_table_alpha,good_pi,_ws));
        }
        if (_opt.is_adis_lrt)
        {
            is_pos_adis = (is_pos_adis || position_strand_distro_anomaly_lrt(_opt.adis_lrt_alpha,good_pi));
        }
    }
    if (_opt.is_acov)
    {
        is_pos_acov = position_strand_coverage_anomaly(_opt.acov_alpha,pi);
    }
#endif

    //    const bool is_nf_snp(is_snp && (! is_filter_snp));
    if (is_snp || is_forced)
    {
        if (_opt.is_compute_hapscore)
        {
            si->hapscore=get_hapscore(pi.hap_set);
        }

        // calculate empirical scoring metrics
        if (_opt.is_compute_germline_scoring_metrics())
        {
            si->mapqRMS = pi.mapqTracker.getRMS();
            si->mapqZeroCount = pi.mapqTracker.zeroCount;
            si->mapqCount = pi.mapqTracker.count;
            si->ReadPosRankSum = pi.get_read_pos_ranksum();
            si->MQRankSum = pi.get_mq_ranksum();
            si->BaseQRankSum = pi.get_baseq_ranksum();
            si->rawPos = pi.get_raw_pos();
            si->avgBaseQ = pi.get_raw_baseQ();
        }

        // hpol filter
        si->hpol = get_snp_hpol_size(pos,_ref);
    }

    if (_opt.is_nonref_sites())
    {
        std::ostream& bos(*_streams.nonref_sites_osptr());
        write_snp_prefix_info_file(_chrom_name,output_pos,pi.get_ref_base(),si->n_used_calls,si->n_unused_calls,bos);
        bos << "\t";
        write_nonref_2allele_test(_opt,good_pi,nrc,bos);
        bos << "\n";
    }

    // report events:
    //
    bool is_reported_event(false);

    std::ostream& report_os(std::cerr);

    if (is_snp)
    {
        if (nrc.is_snp)
        {
            std::ostream& bos(*_streams.nonref_test_osptr());
            write_snp_prefix_info_file(_chrom_name,output_pos,pi.get_ref_base(),si->n_used_calls,si->n_unused_calls,bos);
            bos << "\t";
            write_nonref_2allele_test(_opt,good_pi,nrc,bos);
#if 0
            write_nonref_test(_opt,good_pi,nrc,bos);
#endif
            bos << "\n";
        }
#if 0
        if (lsc.is_snp)
        {
            write_snp_prefix_info("LSNP",output_pos,pi.ref_base,_site_info.n_used_calls,_site_info.n_unused_calls,report_os);
            report_os << " " << lsc << "\n";
        }
#endif
        if (si->dgt.is_snp)
        {
            // this needs to be updated no matter where the snp-call is written to:
            if (_is_variant_windows)
            {
                _variant_print_pos.insert(pos);
                _is_skip_process_pos=false;
            }
        }
#if 0
        if (mgt.is_snp)
        {
            write_snp_prefix_info("BSNP1",output_pos,pi.ref_base,_site_info.n_used_calls,_site_info.n_unused_calls,report_os);
            report_os << " " << mgt << "\n";
        }
        if (ngt_ptr.get() && ngt_ptr->is_snp)
        {
            write_snp_prefix_info("BSNPN",output_pos,pi.ref_base,_site_info.n_used_calls,_site_info.n_unused_calls,report_os);
            report_os << " ";
            nploid_write(*_ninfo,*ngt_ptr,report_os);
            report_os << "\n";
        }
#endif

        is_reported_event = true;
    }

    if (_opt.is_print_all_site_evidence || (_opt.is_print_evidence && is_reported_event))
    {
        report_os << "EVIDENCE pos: " << output_pos << "\n"
                  << "is_snp: " << is_snp << "\n"
                  << pi << "\n";
    }

    //Add site to gvcf
    if (_opt.gvcf.is_gvcf_output())
    {
        _gvcfer->add_site(std::move(si));
    }
}



void
starling_pos_processor::
process_pos_indel_single_sample(
    const pos_t pos,
    const unsigned sample_no)
{
    if (_opt.is_bsnp_diploid())
    {
        process_pos_indel_single_sample_digt(pos,sample_no);
    }
    else
    {
        process_pos_indel_single_sample_continuous(pos,sample_no);
    }
}



void
starling_pos_processor::
process_pos_indel_single_sample_digt(
    const pos_t pos,
    const unsigned sampleId)
{
    // note multi-sample status -- can still be called only for one sample
    // and only for sample 0. working on generalization:
    //
    if (sampleId!=0) return;

    // Current multiploid indel model can handle a het or hom indel
    // allele vs. reference, or two intersecting non-reference indel
    // alleles. (note that indel intersection is evaluated only in
    // terms of breakpoints -- so, for instance, a small het deletion
    // could occur within a large het deletion and the two would be
    // treated as non-interacting -- this is just an artifact of how
    // the methods are coded,)
    //

    std::ostream& report_os(std::cerr);

    sample_info& sif(sample(sampleId));
    auto it(getIndelBuffer().positionIterator(pos));
    const auto it_end(getIndelBuffer().positionIterator(pos + 1));

    for (; it!=it_end; ++it)
    {
        const indel_key& ik(it->first);
        const IndelData& id(getIndelData(it));
        const bool isForcedOutput(id.is_forced_output);

        const IndelSampleData& isd(id.getSampleData(sampleId));
        const bool isZeroCoverage(isd.read_path_lnp.empty());

        if (! isForcedOutput)
        {
            if (isZeroCoverage) continue;
            if (!getIndelBuffer().isCandidateIndel(ik, id)) continue;
        }

        // TODO implement indel overlap resolution
        //
        // punt conflict resolution for now....
        {
            // indel_report_info needs to be run first now so that
            // local small repeat info is available to the indel
            // caller

            // sample-independent info:
            starling_indel_report_info iri;
            get_starling_indel_report_info(ik,id,_ref,iri);

            // STARKA-248 filter invalid indel
            /// TODO: filter this issue earlier (occurs as, e.g. 1D1I which matches ref)
            if (iri.vcf_indel_seq == iri.vcf_ref_seq) continue;

            static const bool is_tier2_pass(false);
            static const bool is_use_alt_indel(true);

            starling_diploid_indel dindel;
            dindel.is_forced_output = isForcedOutput;
            dindel.is_zero_coverage = isZeroCoverage;

            {
                // check whether we're in a haploid/noploid region, for indels just check
                // start position and end position, approximating that the whole
                // region in between has the same ploidy, for any anomalous state
                // revert to 'noploid':
                const int indelLeftPloidy(get_ploidy(ik.pos));
                const int indelRightPloidy(get_ploidy(ik.right_pos()));

                if (indelLeftPloidy == indelRightPloidy)
                {
                    dindel.ploidy = indelLeftPloidy;
                }
                else
                {
                    dindel.ploidy = 0;
                }
            }

            _dopt.incaller().starling_indel_call_pprob_digt(
                _opt,_dopt,
                sif.sample_opt,
                id.errorRates.scaledRefToIndelErrorProb,id.errorRates.scaledIndelToRefErrorProb,
                ik,isd,is_use_alt_indel,dindel);

            bool is_indel(false);
            if ((dindel.is_indel) || (dindel.is_forced_output))
            {
                is_indel=true;

                // sample-specific info: (division doesn't really matter
                // in single-sample case)
                starling_indel_sample_report_info isri;
                get_starling_indel_sample_report_info(_dopt,ik,isd,sif.bc_buff,
                                                      is_tier2_pass,is_use_alt_indel,isri);

                if (_opt.gvcf.is_gvcf_output())
                {
                    assert(ik.pos==pos);
                    _gvcfer->add_indel(std::unique_ptr<GermlineIndelCallInfo>(new GermlineDiploidIndelCallInfo(ik,id, dindel,iri,isri)));
                }

                if (_is_variant_windows)
                {
                    _variant_print_pos.insert(pos);
                    _is_skip_process_pos=false;
                }
            }

            /// \TODO put this option under runtime control...
            /// \TODO setup option so that read keys persist longer when needed for this case...
            ///
            static const bool is_print_indel_evidence(false);

            if (is_print_indel_evidence && is_indel)
            {
                report_os << "INDEL_EVIDENCE " << ik;

                for (const auto& val : isd.read_path_lnp)
                {
                    const align_id_t read_id(val.first);
                    const ReadPathScores& lnp(val.second);
                    const ReadPathScores pprob(indel_lnp_to_pprob(_dopt,lnp,is_tier2_pass,is_use_alt_indel));
                    const starling_read* srptr(sif.read_buff.get_read(read_id));

                    report_os << "read key: ";
                    if (NULL==srptr) report_os << "UNKNOWN_KEY";
                    else            report_os << srptr->key();
                    report_os << "\n"
                              << "read log_lhoods: " << lnp << "\n"
                              << "read pprobs: " << pprob << "\n";
                }
            }
        }
    }
}

void
starling_pos_processor::
process_pos_indel_single_sample_continuous(
    const pos_t pos,
    const unsigned sampleId)
{
    // note multi-sample status -- can still be called only for one sample
    // and only for sample 0. working on generalization:
    //
    if (sampleId!=0) return;

    sample_info& sif(sample(sampleId));
    auto it(getIndelBuffer().positionIterator(pos));
    const auto it_end(getIndelBuffer().positionIterator(pos + 1));

    std::unique_ptr<GermlineContinuousIndelCallInfo> info;

    for (; it!=it_end; ++it)
    {
        const indel_key& ik(it->first);
        const IndelData& id(getIndelData(it));
        const bool isForcedOutput(id.is_forced_output);

        const IndelSampleData& isd(id.getSampleData(sampleId));
        const bool isZeroCoverage(isd.read_path_lnp.empty());

        if (! isForcedOutput)
        {
            if (isZeroCoverage) continue;
            if (!getIndelBuffer().isCandidateIndel(ik, id)) continue;
        }

        // sample-independent info:
        starling_indel_report_info iri;
        get_starling_indel_report_info(ik,id,_ref,iri);

        static const bool is_tier2_pass(false);
        static const bool is_use_alt_indel(true);

        if (!info)
            info.reset(new GermlineContinuousIndelCallInfo(pos));

        starling_indel_sample_report_info isri;
        get_starling_indel_sample_report_info(_dopt,ik,isd,sif.bc_buff, is_tier2_pass,is_use_alt_indel,isri);
        starling_continuous_variant_caller::add_indel_call(_opt, ik, id, iri, isri, *info);

    }
    if (info && (info->is_indel() || info->is_forced_output()))
    {
        if (_opt.gvcf.is_gvcf_output())
        {
            _gvcfer->add_indel(std::move(info));
        }

        if (_is_variant_windows)
        {
            _variant_print_pos.insert(pos);
            _is_skip_process_pos=false;
        }
    }
}




void
starling_pos_processor::
write_counts(const pos_range& output_report_range) const
{
    std::ostream* report_osptr(get_report_osptr());
    if (NULL==report_osptr) return;
    std::ostream& report_os(*report_osptr);

    const sample_info& sif(sample());

    report_os << std::setprecision(8);
    report_stream_stat(sif.ss,"ALLSITES_COVERAGE",output_report_range,report_os);
    report_stream_stat(sif.used_ss,"ALLSITES_COVERAGE_USED",output_report_range,report_os);

    if (_opt.is_ref_set())
    {
        report_stream_stat(sif.ssn,"NO_REF_N_COVERAGE",output_report_range,report_os);
        report_stream_stat(sif.used_ssn,"NO_REF_N_COVERAGE_USED",output_report_range,report_os);
    }
}
