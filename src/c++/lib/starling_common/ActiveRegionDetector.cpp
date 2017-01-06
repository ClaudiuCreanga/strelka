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
/// \author Sangtae Kim
///

#include "ActiveRegionDetector.hh"

void ActiveRegionDetector::insertMatch(const align_id_t alignId, const pos_t pos)
{
    addVariantCount(getSampleId(alignId), pos, 0);
    setMatch(alignId, pos);
    addAlignIdToPos(alignId, pos);
}

void ActiveRegionDetector::insertSoftClipSegment(const align_id_t alignId, const pos_t pos, const std::string& segmentSeq, bool isBeginEdge)
{
    // For invariant counting
    addVariantCount(getSampleId(alignId), pos, 1);
    if (isBeginEdge)
        addVariantCount(getSampleId(alignId), pos+1, 1);
    else
        addVariantCount(getSampleId(alignId), pos-1, 1);

    // soft clipp doesn't add mismatch count, but the base is used in haplotype generation
    setSoftClipSegment(alignId, pos, segmentSeq);
    addAlignIdToPos(alignId, pos);
}

void
ActiveRegionDetector::insertMismatch(const align_id_t alignId, const pos_t pos, const char baseChar)
{
    addVariantCount(getSampleId(alignId), pos, MismatchWeight);
    setMismatch(alignId, pos, baseChar);
    addAlignIdToPos(alignId, pos);
}

void
ActiveRegionDetector::insertIndel(const unsigned sampleId, const IndelObservation& indelObservation)
{
    auto pos = indelObservation.key.pos;

    auto alignId = indelObservation.data.id;
    auto indelKey = indelObservation.key;

    if (!indelObservation.data.is_low_map_quality)
    {
        if (indelKey.isPrimitiveInsertionAllele())
        {
            addVariantCount(sampleId, pos - 1, IndelWeight);
            addVariantCount(sampleId, pos, IndelWeight);
            setInsert(alignId, pos - 1, indelObservation.key.insert_seq());
            addAlignIdToPos(alignId, pos - 1);
        }
        else if (indelKey.isPrimitiveDeletionAllele())
        {
            unsigned length = indelObservation.key.deletionLength;
            for (unsigned i(0); i<length; ++i)
            {
                addVariantCount(sampleId, pos + i, IndelWeight);
                setDelete(alignId, pos + i);
                addAlignIdToPos(alignId, pos + i);
            }
            addVariantCount(sampleId, pos - 1, IndelWeight);
        }
        else
        {
            // ignore BP_LEFT, BP_RIGHT, SWAP
        }
    }
    _indelBuffer.addIndelObservation(sampleId, indelObservation);
}

void
ActiveRegionDetector::updateStartPosition(const pos_t pos)
{
    if (_activeRegions.empty()) return;

    if (_activeRegions.front().getBeginPosition() == pos)
    {
        _activeRegions.pop_front();
    }

    for (unsigned sampleId(0); sampleId<_sampleCount; ++sampleId)
        _polySites[sampleId].eraseTo(pos);
}

void
ActiveRegionDetector::updateEndPosition(const pos_t pos, const bool isLastPos)
{
    const pos_t posToClear = pos - _maxDetectionWindowSize - _maxIndelSize;

    if (pos < _lastActiveRegionEnd)
    {
        // this position was already covered by the previous active region
        _activeRegionStartPos = 0;
        _numVariants = 0;
        clearPos(posToClear);
        return;
    }

//    bool isCurrentPosCandidateVariant = isCandidateVariant(pos);
    bool isCurrentPosCandidateVariant = !(isInvariant(pos));

    // check if we can include this position in the existing acitive region
    bool isSizeFit = (pos - _activeRegionStartPos) < (int)_maxDetectionWindowSize;
    auto distanceFromPrevVariant = pos - _prevVariantPos;
    bool isConsecutiveVariant = (distanceFromPrevVariant == 1); // size may exceed _maxDetectionWindowSize for consecutive variants
    bool isNotFarFromPrevVariant = (pos > 0) && (distanceFromPrevVariant <= MaxDistanceBetweenTwoVariants);

    bool isExtensible = (isSizeFit || isConsecutiveVariant) && isNotFarFromPrevVariant;

    if (isExtensible && !isLastPos)  // if pos is the last position, we cannot extend
    {
        // this position extends the existing active region
        if (isCurrentPosCandidateVariant)
        {
            ++_numVariants;
            _prevVariantPos = pos;
        }
    }
    else
    {
        if (isLastPos && isExtensible && isCurrentPosCandidateVariant)
        {
            ++_numVariants;
            _prevVariantPos = pos;
        }

        // this position doesn't extend the existing active region
        if (_numVariants >= _minNumVariantsPerRegion)
        {
            pos_t origBeginPos = _activeRegionStartPos;
            pos_t origEndPos = _prevVariantPos + 1;
            pos_range newActiveRegion;

            // expand active region to include repeats
            getExpandedRange(pos_range(origBeginPos, origEndPos), newActiveRegion);
            _lastActiveRegionEnd = newActiveRegion.end_pos;

            _activeRegions.emplace_back(newActiveRegion, _ref, _maxIndelSize, _sampleCount, _aligner, _alignIdToAlignInfo);
            auto& activeRegion(_activeRegions.back());
            // add haplotype bases
            for (pos_t activeRegionPos(newActiveRegion.begin_pos); activeRegionPos<newActiveRegion.end_pos; ++activeRegionPos)
            {
                for (const align_id_t alignId : getPositionToAlignIds(activeRegionPos))
                {
                    std::string haplotypeBase;
                    bool isSoftClipped = setHaplotypeBase(alignId, activeRegionPos, haplotypeBase);
                    if (isSoftClipped)
                        activeRegion.setSoftClipped(alignId);
                    activeRegion.insertHaplotypeBase(alignId, activeRegionPos, haplotypeBase);
                }
            }
            activeRegion.processHaplotypes(_indelBuffer, _polySites);
        }

        if (isCurrentPosCandidateVariant)
        {
            // start new active region
            _activeRegionStartPos = pos;
            _numVariants = 1;
            _prevVariantPos = pos;
        }
        else
        {
            _activeRegionStartPos = 0;
            _numVariants = 0;
        }
    }

    clearPos(posToClear);
}

void ActiveRegionDetector::getExpandedRange(const pos_range& origActiveRegion, pos_range& newActiveRegion)
{
    // if origStart is within repeat region, move the start position outside of the repeat region
    // e.g. In TACGACGAC|GAC if origStart points C before |, the start position is moved to T
    // note that bases after origStart are ignored. For example, in TACGAC|GAC, the start position doesn't move.
    pos_t origStart = origActiveRegion.begin_pos;
    pos_t deltaPos(0);
    for (unsigned repeatUnitLength(1); repeatUnitLength<=MaxRepeatUnitLength; ++repeatUnitLength)
    {
        pos_t repeatSpan = repeatUnitLength;
        for (pos_t pos(origStart-repeatUnitLength); pos >= _ref.get_offset(); --pos)
        {
            char baseChar = _ref.get_base(pos);
            char baseCharToCompare = _ref.get_base(pos+repeatUnitLength);
            if (baseChar != baseCharToCompare)
                break;
            ++repeatSpan;
        }
        unsigned repeatLength = repeatSpan / repeatUnitLength;
        if (repeatLength > 1)
            deltaPos = std::max(deltaPos, repeatSpan);
    }
    deltaPos = std::min(deltaPos, MaxRepeatSpan);
    const pos_t minStartLowerBound(std::max(0,_lastActiveRegionEnd-1));
    const pos_t minStart(std::max(origStart - deltaPos, minStartLowerBound));
    pos_t newBeginPos;
    for (newBeginPos = origStart; newBeginPos > minStart; --newBeginPos)
    {
        bool isLowDepth = false;
        for (unsigned sampleId(0); sampleId<_sampleCount; ++sampleId)
        {
            if (getDepth(sampleId, newBeginPos-1) < MinDepth)
            {
                isLowDepth = true;
                break;
            }
        }
        if (isLowDepth) break;
    }
    newActiveRegion.set_begin_pos(newBeginPos);

    // calculate newEnd
    pos_t origEnd = origActiveRegion.end_pos;
    deltaPos = 0;
    for (unsigned repeatUnitLength(1); repeatUnitLength<=MaxRepeatUnitLength; ++repeatUnitLength)
    {
        pos_t repeatSpan = repeatUnitLength;
        for (pos_t pos(origEnd+repeatUnitLength-1); pos < _ref.end(); ++pos)
        {
            char baseChar = _ref.get_base(pos);
            char baseCharToCompare = _ref.get_base(pos-repeatUnitLength);
            if (baseChar != baseCharToCompare)
                break;
            ++repeatSpan;
        }
        unsigned repeatLength = repeatSpan / repeatUnitLength;
        if (repeatLength > 1)
            deltaPos = std::max(deltaPos, repeatSpan);
    }
    deltaPos = std::min(deltaPos, MaxRepeatSpan);

    pos_t maxEnd(std::min(origEnd + deltaPos, _ref.end()));
    pos_t newEndPos;
    for (newEndPos = origEnd; newEndPos < maxEnd; ++newEndPos)
    {
        bool isLowDepth = false;
        for (unsigned sampleId(0); sampleId<_sampleCount; ++sampleId)
        {
            if (getDepth(sampleId, newBeginPos-1) < MinDepth)
            {
                isLowDepth = true;
                break;
            }
        }
        if (isLowDepth) break;
    }
    newActiveRegion.set_end_pos(newEndPos);
}

void ActiveRegionDetector::setMatch(const align_id_t id, const pos_t pos)
{
    _variantInfo[id % MaxDepth][pos % MaxBufferSize] = MATCH;
}

void ActiveRegionDetector::setMismatch(const align_id_t id, const pos_t pos, char baseChar)
{
    unsigned idIndex = id % MaxDepth;
    unsigned posIndex = pos % MaxBufferSize;
    _variantInfo[idIndex][posIndex] = MISMATCH;
    _snvBuffer[idIndex][posIndex] = baseChar;
}

void ActiveRegionDetector::setSoftClipSegment(const align_id_t id, const pos_t pos, const std::string& segmentSeq)
{
    unsigned idIndex = id % MaxDepth;
    unsigned posIndex = pos % MaxBufferSize;
    _variantInfo[idIndex][posIndex] = SOFT_CLIP;
    _insertSeqBuffer[idIndex][posIndex] = segmentSeq;
}

void ActiveRegionDetector::setDelete(const align_id_t id, const pos_t pos)
{
    _variantInfo[id % MaxDepth][pos % MaxBufferSize] = DELETE;
}

void ActiveRegionDetector::setInsert(const align_id_t id, const pos_t pos, const std::string& insertSeq)
{
    unsigned idIndex = id % MaxDepth;
    unsigned posIndex = pos % MaxBufferSize;
    _variantInfo[idIndex][posIndex] = (_variantInfo[idIndex][posIndex] == MISMATCH ? MISMATCH_INSERT : INSERT);
    _insertSeqBuffer[idIndex][posIndex] = insertSeq;
}

bool ActiveRegionDetector::setHaplotypeBase(const align_id_t id, const pos_t pos, std::string& base) const
{
    unsigned idIndex = id % MaxDepth;
    unsigned posIndex = pos % MaxBufferSize;
    const auto variant = _variantInfo[idIndex][posIndex];
    switch (variant)
    {
    case MATCH:
        base = _ref.get_base(pos);
        break;
    case MISMATCH:
        base = std::string(1, _snvBuffer[idIndex][posIndex]);
        break;
    case DELETE:
        base = "";
        break;
    case INSERT:
        base = _ref.get_base(pos) + _insertSeqBuffer[idIndex][posIndex];
        break;
    case SOFT_CLIP:
        base = _insertSeqBuffer[idIndex][posIndex];
        break;
    case MISMATCH_INSERT:
        base = _snvBuffer[idIndex][posIndex] + _insertSeqBuffer[idIndex][posIndex];
    }

    bool isSoftClipped = (variant == SOFT_CLIP);
    return isSoftClipped;
}

bool
ActiveRegionDetector::isCandidateVariant(const pos_t pos) const
{
    for (unsigned sampleId(0); sampleId<_sampleCount; ++sampleId)
    {
        auto count = getVariantCount(sampleId, pos);
        if (count >= _minNumVariantsPerPositionPerSample
            && count >= (MinAlternativeAlleleFraction*getDepth(sampleId, pos)))
            return true;
    }
    return false;
}

bool
ActiveRegionDetector::isInvariant(const pos_t pos) const
{
    for (unsigned sampleId(0); sampleId<_sampleCount; ++sampleId)
    {
        auto count = getVariantCount(sampleId, pos);
        if (count >= 4
            || count >= (0.1*getDepth(sampleId, pos)))
            return false;
    }
    return true;
}

bool ActiveRegionDetector::isPolymorphicSite(const unsigned sampleId, const pos_t pos) const
{
    return _polySites[sampleId].isKeyPresent(pos);
}
