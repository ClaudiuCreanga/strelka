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
/*
 *  Created on: Jun 4, 2015
 *      Author: jduddy
 */

#include "variant_prefilter_stage.hh"

#include "ScoringModelManager.hh"

variant_prefilter_stage::variant_prefilter_stage(const ScoringModelManager& model, std::shared_ptr<variant_pipe_stage_base> destination)
    : variant_pipe_stage_base(destination)
    , _model(model)
{
}

static
void
set_site_gt(const diploid_genotype::result_set& rs,
            GermlineDiploidSiteAlleleInfo& allele)
{
    allele.max_gt=rs.max_gt;
    allele.gqx=rs.max_gt_qphred;
}


void
variant_prefilter_stage::
add_site_modifiers(
    GermlineDiploidSiteLocusInfo& si,
    GermlineDiploidSiteAlleleInfo& allele,
    const ScoringModelManager& model)
{
    allele.clear();
    allele.is_unknown=(si.ref=='N');

    /// TODO STREL-125 generalize to multi-sample
    auto& sampleInfo(si.getSample(0));

    allele.is_used_covered=(si.n_used_calls!=0);
    allele.is_covered=(si.allele.is_used_covered || si.n_unused_calls!=0);
    allele.strand_bias=si.dgt.strand_bias;

    if     (allele.is_unknown)
    {
        allele.gqx=0;
        sampleInfo.gq=0;
        allele.max_gt=0;
    }
    else if (si.dgt.genome.max_gt != si.dgt.poly.max_gt)
    {
        allele.gqx=0;
        sampleInfo.gq=si.dgt.poly.max_gt_qphred;
        allele.max_gt=si.dgt.poly.max_gt;
    }
    else
    {
        if (si.dgt.genome.max_gt_qphred<si.dgt.poly.max_gt_qphred)
        {
            set_site_gt(si.dgt.genome,allele);
        }
        else
        {
            set_site_gt(si.dgt.poly,allele);
        }
        sampleInfo.gq=si.dgt.poly.max_gt_qphred;
    }

    model.classify_site(si, allele);
}

void variant_prefilter_stage::process(std::unique_ptr<GermlineSiteLocusInfo> info)
{
    if (dynamic_cast<GermlineDiploidSiteLocusInfo*>(info.get()) != nullptr)
    {
        auto si(downcast<GermlineDiploidSiteLocusInfo>(std::move(info)));

        add_site_modifiers(*si, si->allele, _model);
        if (si->dgt.is_haploid())
        {
            if (si->allele.max_gt == si->dgt.ref_gt)
            {
                si->allele.modified_gt=MODIFIED_SITE_GT::ZERO;
            }
            else
            {
                si->allele.modified_gt=MODIFIED_SITE_GT::ONE;
            }
        }
        else if (si->dgt.is_noploid())
        {
            if (! si->is_print_unknowngt())
            {
                si->filters.set(GERMLINE_VARIANT_VCF_FILTERS::PloidyConflict);
            }
        }

        _sink->process(std::move(si));
    }
    else
    {
        auto si(downcast<GermlineContinuousSiteLocusInfo>(std::move(info)));
        for (auto& altAllele : si->altAlleles)
        {
            _model.default_classify_site(*si, altAllele);
        }

        _sink->process(std::move(si));
    }
}

void variant_prefilter_stage::process(std::unique_ptr<GermlineIndelLocusInfo> info)
{
    if (dynamic_cast<GermlineDiploidIndelLocusInfo*>(info.get()) != nullptr)
    {
        auto ii(downcast<GermlineDiploidIndelLocusInfo>(std::move(info)));

        // we can't handle breakends at all right now:
        if (ii->getFirstAltAllele()._indelKey.is_breakpoint()) return;

        // add filter for all indels in no-ploid regions:
        const unsigned sampleCount(ii->getSampleCount());
        for (unsigned sampleIndex(0); sampleIndex < sampleCount; ++sampleIndex)
        {
            LocusSampleInfo& sampleInfo(ii->getSample(sampleIndex));
            if (sampleInfo.getPloidy().isNoploid())
            {
                sampleInfo.filters.set(GERMLINE_VARIANT_VCF_FILTERS::PloidyConflict);
            }
        }

        _sink->process(std::move(ii));
    }
    else
    {
        auto ii(downcast<GermlineContinuousIndelLocusInfo>(std::move(info)));

        // we can't handle breakends at all right now:
        for (const auto& altAllele : ii->altAlleles)
        {
            if (altAllele._indelKey.is_breakpoint()) return;
        }

        for (const auto& altAllele : ii->altAlleles)
        {
            _model.default_classify_indel(*ii, altAllele);
        }
        _sink->process(std::move(ii));
    }
}

