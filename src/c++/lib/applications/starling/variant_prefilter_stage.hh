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
 *
 *  Created on: Jun 4, 2015
 *      Author: jduddy
 */
#pragma once
#include "variant_pipe_stage_base.hh"

class calibration_models;

class variant_prefilter_stage : public variant_pipe_stage_base
{
public:
    variant_prefilter_stage(const calibration_models& model, std::shared_ptr<variant_pipe_stage_base> destination);
    void process(std::unique_ptr<site_info> si) override;
    void process(std::unique_ptr<indel_info> ii) override;

    static void  add_site_modifiers(
        const digt_site_info& si,
        digt_call_info& smod,
        const calibration_models& model);

private:
    const calibration_models& _model;
};
