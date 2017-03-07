// -*- mode: c++; indent-tabs-mode: nil; -*-
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

#include "starling_common/starling_base_shared.hh"
#include "blt_common/snp_pos_info.hh"

#include "gvcf_locus_info.hh"


struct starling_continuous_variant_caller
{
    static
    int
    poisson_qscore(unsigned callCount, unsigned coverage, unsigned estimatedBaseCallQuality, int maxQScore);

    /// return log likelihood ratio of the variants on either strand over both strands
    static
    double
    strandBias(
        unsigned fwdAlt,
        unsigned revAlt,
        unsigned fwdOther,
        unsigned revOther);
};
