// -*- mode: c++; indent-tabs-mode: nil; -*-
//
// Starka
// Copyright (c) 2009-2014 Illumina, Inc.
//
// This software is provided under the terms and conditions of the
// Illumina Open Source Software License 1.
//
// You should have received a copy of the Illumina Open Source
// Software License 1 along with this program. If not, see
// <https://github.com/sequencing/licenses/>
//

///
/// \author Chris Saunders
///

#include "inovo_option_parser.hh"
#include "DenovoAlignmentFileOptionsParser.hh"

#include "blt_common/blt_arg_validate.hh"
#include "starling_common/starling_base_option_parser.hh"



po::options_description
get_inovo_option_parser(
    inovo_options& opt)
{
    po::options_description inovo_parse_opt_ti(getOptionsDescription(opt.alignFileOpt));

    po::options_description inovo_parse_opt_sv("De-novo variant-calling");
    inovo_parse_opt_sv.add_options()
    ("denovo-file",
     po::value(&opt.denovo_filename),
     "Vcf output file de-novo small-variants")
    ;

    po::options_description inovo_parse_opt_filter("De-novo variant-calling filters");
    inovo_parse_opt_filter.add_options()
    ("inovo-chrom-depth-file", po::value(&opt.dfilter.chrom_depth_file),
     "If provided, the mean depth for each chromosome will be read from file, and these values will be used for high depth filtration. File should contain one line per chromosome, where each line begins with: \"chrom_name<TAB>depth\" (default: no chrom depth filtration)")
    ("inovo-max-depth-factor", po::value(&opt.dfilter.max_depth_factor)->default_value(opt.dfilter.max_depth_factor),
     "If a chrom depth file is supplied then loci with depth exceeding the mean chromosome depth times this value are filtered")
    ("inovo-skip-header", po::value(&opt.dfilter.is_skip_header)->zero_tokens(),
     "Skip writing header info for all de-novo vcf/bed files (usually used to simplify segment concatenation)")
    ;

    po::options_description inovo_parse_opt("De-novo caller options");
    inovo_parse_opt.add(inovo_parse_opt_ti).add(inovo_parse_opt_sv).add(inovo_parse_opt_filter);

    // final assembly
    po::options_description visible("Options");
    visible.add(inovo_parse_opt);

    // add starling base options:
    po::options_description visible2(get_starling_base_option_parser(opt));
    visible.add(visible2);

    po::options_description help_parse_opt("Help");
    help_parse_opt.add_options()
    ("help,h","print this message");

    visible.add(help_parse_opt);

    return visible;

}



void
finalize_inovo_options(
    const prog_info& pinfo,
    const po::variables_map& vm,
    inovo_options& opt)
{
    std::string errorMsg;
    if (parseOptions(vm, opt.alignFileOpt, errorMsg))
    {
        pinfo.usage(errorMsg.c_str());
    }

    // deal with sfilter options:
    if (opt.dfilter.max_depth_factor < 0)
    {
        pinfo.usage("max-depth-factor must not be less than 0");
    }

    if (! opt.bam_filename.empty())
    {
        pinfo.usage("standard bam input not accepted for de-novo calling");
    }

    finalize_starling_base_options(pinfo,vm,opt);
}