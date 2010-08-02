/**
 *  @file   PandoraPFANew/src/TrackCreator.cc
 * 
 *  @brief  Implementation of the track creator class.
 * 
 *  $Log: $
 */

#include "marlin/Global.h"
#include "marlin/Processor.h"

#include "EVENT/LCCollection.h"
#include "EVENT/ReconstructedParticle.h"
#include "EVENT/Vertex.h"

#include "gear/BField.h"
#include "gear/CalorimeterParameters.h"
#include "gear/PadRowLayout2D.h"
#include "gear/TPCParameters.h"

#include "ClusterShapes.h"
#include "HelixClass.h"

#include "PandoraPFANewProcessor.h"
#include "TrackCreator.h"
#include "Pandora/PdgTable.h"

#include <algorithm>
#include <cmath>
#include <limits>


TrackVector TrackCreator::m_trackVector;

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode TrackCreator::CreateTrackAssociations(const LCEvent *const pLCEvent)
{
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, this->ExtractKinks(pLCEvent));
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, this->ExtractProngsAndSplits(pLCEvent));
    PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, this->ExtractV0s(pLCEvent));

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode TrackCreator::ExtractKinks(const LCEvent *const pLCEvent)
{
    static pandora::Pandora *pPandora = PandoraPFANewProcessor::GetPandora();

    for (StringVector::const_iterator iter = m_settings.m_kinkVertexCollections.begin(), iterEnd = m_settings.m_kinkVertexCollections.end();
        iter != iterEnd; ++iter)
    {
        try
        {
            const LCCollection *pKinkCollection = pLCEvent->getCollection(*iter);

            for (int i = 0, iMax = pKinkCollection->getNumberOfElements(); i < iMax; ++i)
            {
                try
                {
                    Vertex *pVertex = dynamic_cast<Vertex*>(pKinkCollection->getElementAt(i));

                    ReconstructedParticle *pReconstructedParticle = pVertex->getAssociatedParticle();
                    const TrackVec &trackVec(pReconstructedParticle->getTracks());

                    if (this->IsConflictingRelationship(trackVec))
                        continue;

                    const int vertexPdgCode(pReconstructedParticle->getType());

                    // Extract the kink vertex information
                    for (unsigned int iTrack = 0, nTracks = trackVec.size(); iTrack < nTracks; ++iTrack)
                    {
                        Track *pTrack = trackVec[iTrack];
                        (0 == iTrack) ? m_parentTrackList.insert(pTrack) : m_daughterTrackList.insert(pTrack);
                        streamlog_out(DEBUG) << "KinkTrack " << iTrack << ", nHits " << pTrack->getTrackerHits().size() << std::endl;

                        int trackPdgCode = pandora::UNKNOWN;

                        if (0 == iTrack)
                        {
                            trackPdgCode = vertexPdgCode;
                        }
                        else
                        {
                            switch (vertexPdgCode)
                            {
                            case pandora::PI_PLUS :
                            case pandora::K_PLUS :
                                trackPdgCode = pandora::MU_PLUS;
                                break;
                            case pandora::PI_MINUS :
                            case pandora::K_MINUS :
                                trackPdgCode = pandora::MU_MINUS;
                                break;
                            case pandora::HYPERON_MINUS_BAR :
                            case pandora::SIGMA_PLUS :
                                trackPdgCode = pandora::PI_PLUS;
                                break;
                            case pandora::SIGMA_MINUS :
                            case pandora::HYPERON_MINUS :
                                trackPdgCode = pandora::PI_PLUS;
                                break;
                            default :
                                (pTrack->getOmega() > 0) ? trackPdgCode = pandora::PI_PLUS : trackPdgCode = pandora::PI_MINUS;
                                break;
                            }
                        }

                        m_trackToPidMap.insert(TrackToPidMap::value_type(pTrack, trackPdgCode));

                        if (0 == m_settings.m_shouldFormTrackRelationships)
                            continue;

                        // Make track parent-daughter relationships
                        if (0 == iTrack)
                        {
                            for (unsigned int jTrack = iTrack + 1; jTrack < nTracks; ++jTrack)
                            {
                                PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraApi::SetTrackParentDaughterRelationship(*pPandora,
                                    pTrack, trackVec[jTrack]));
                            }
                        }

                        // Make track sibling relationships
                        else
                        {
                            for (unsigned int jTrack = iTrack + 1; jTrack < nTracks; ++jTrack)
                            {
                                PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraApi::SetTrackSiblingRelationship(*pPandora,
                                    pTrack, trackVec[jTrack]));
                            }
                        }
                    }
                }
                catch (...)
                {
                    streamlog_out(WARNING) << "Failed to extract kink vertex, unrecognised exception" << std::endl;
                }
            }
        }
        catch (...)
        {
            streamlog_out(MESSAGE) << "Failed to extract kink vertex collection: " << *iter << std::endl;
        }
    }

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode TrackCreator::ExtractProngsAndSplits(const LCEvent *const pLCEvent)
{
    static pandora::Pandora *pPandora = PandoraPFANewProcessor::GetPandora();

    for (StringVector::const_iterator iter = m_settings.m_prongSplitVertexCollections.begin(), iterEnd = m_settings.m_prongSplitVertexCollections.end();
        iter != iterEnd; ++iter)
    {
        try
        {
            const LCCollection *pProngOrSplitCollection = pLCEvent->getCollection(*iter);

            for (int i = 0, iMax = pProngOrSplitCollection->getNumberOfElements(); i < iMax; ++i)
            {
                try
                {
                    Vertex *pVertex = dynamic_cast<Vertex*>(pProngOrSplitCollection->getElementAt(i));

                    ReconstructedParticle *pReconstructedParticle = pVertex->getAssociatedParticle();
                    const TrackVec &trackVec(pReconstructedParticle->getTracks());

                    if (this->IsConflictingRelationship(trackVec))
                        continue;

                    // Extract the prong/split vertex information
                    for (unsigned int iTrack = 0, nTracks = trackVec.size(); iTrack < nTracks; ++iTrack)
                    {
                        Track *pTrack = trackVec[iTrack];
                        (0 == iTrack) ? m_parentTrackList.insert(pTrack) : m_daughterTrackList.insert(pTrack);
                        streamlog_out(DEBUG) << "Prong or Split Track " << iTrack << ", nHits " << pTrack->getTrackerHits().size() << std::endl;

                        if (0 == m_settings.m_shouldFormTrackRelationships)
                            continue;

                        // Make track parent-daughter relationships
                        if (0 == iTrack)
                        {
                            for (unsigned int jTrack = iTrack + 1; jTrack < nTracks; ++jTrack)
                            {
                                PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraApi::SetTrackParentDaughterRelationship(*pPandora,
                                    pTrack, trackVec[jTrack]));
                            }
                        }

                        // Make track sibling relationships
                        else
                        {
                            for (unsigned int jTrack = iTrack + 1; jTrack < nTracks; ++jTrack)
                            {
                                PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraApi::SetTrackSiblingRelationship(*pPandora,
                                    pTrack, trackVec[jTrack]));
                            }
                        }
                    }
                }
                catch (...)
                {
                    streamlog_out(WARNING) << "Failed to extract prong/split vertex, unrecognised exception" << std::endl;
                }
            }
        }
        catch (...)
        {
            streamlog_out(MESSAGE) << "Failed to extract prong/split vertex collection: " << *iter << std::endl;
        }
    }

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode TrackCreator::ExtractV0s(const LCEvent *const pLCEvent)
{
    static pandora::Pandora *pPandora = PandoraPFANewProcessor::GetPandora();

    for (StringVector::const_iterator iter = m_settings.m_v0VertexCollections.begin(), iterEnd = m_settings.m_v0VertexCollections.end();
        iter != iterEnd; ++iter)
    {
        try
        {
            const LCCollection *pV0Collection = pLCEvent->getCollection(*iter);

            for (int i = 0, iMax = pV0Collection->getNumberOfElements(); i < iMax; ++i)
            {
                try
                {
                    Vertex *pVertex = dynamic_cast<Vertex*>(pV0Collection->getElementAt(i));

                    ReconstructedParticle *pReconstructedParticle = pVertex->getAssociatedParticle();
                    const TrackVec &trackVec(pReconstructedParticle->getTracks());

                    if (this->IsConflictingRelationship(trackVec))
                        continue;

                    // Extract the v0 vertex information
                    const int vertexPdgCode(pReconstructedParticle->getType());

                    for (unsigned int iTrack = 0, nTracks = trackVec.size(); iTrack < nTracks; ++iTrack)
                    {
                        Track *pTrack = trackVec[iTrack];
                        m_v0TrackList.insert(pTrack);
                        streamlog_out(DEBUG) << "V0Track " << iTrack << ", nHits " << pTrack->getTrackerHits().size() << std::endl;

                        int trackPdgCode = pandora::UNKNOWN;

                        switch (vertexPdgCode)
                        {
                        case pandora::PHOTON :
                            (pTrack->getOmega() > 0) ? trackPdgCode = pandora::E_PLUS : trackPdgCode = pandora::E_MINUS;
                            break;
                        case pandora::LAMBDA :
                            (pTrack->getOmega() > 0) ? trackPdgCode = pandora::PROTON : trackPdgCode = pandora::PI_MINUS;
                            break;
                        case pandora::LAMBDA_BAR :
                            (pTrack->getOmega() > 0) ? trackPdgCode = pandora::PI_PLUS : trackPdgCode = pandora::PROTON_BAR;
                            break;
                        case pandora::K_SHORT :
                            (pTrack->getOmega() > 0) ? trackPdgCode = pandora::PI_PLUS : trackPdgCode = pandora::PI_MINUS;
                            break;
                        default :
                            (pTrack->getOmega() > 0) ? trackPdgCode = pandora::PI_PLUS : trackPdgCode = pandora::PI_MINUS;
                            break;
                        }

                        m_trackToPidMap.insert(TrackToPidMap::value_type(pTrack, trackPdgCode));

                        if (0 == m_settings.m_shouldFormTrackRelationships)
                            continue;

                        // Make track sibling relationships
                        for (unsigned int jTrack = iTrack + 1; jTrack < nTracks; ++jTrack)
                        {
                            PANDORA_RETURN_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraApi::SetTrackSiblingRelationship(*pPandora,
                                pTrack, trackVec[jTrack]));
                        }
                    }
                }
                catch (...)
                {
                    streamlog_out(WARNING) << "Failed to extract v0 vertex, unrecognised exception" << std::endl;
                }
            }
        }
        catch (...)
        {
            streamlog_out(MESSAGE) << "Failed to extract v0 vertex collection: " << *iter << std::endl;
        }
    }

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

bool TrackCreator::IsConflictingRelationship(const TrackVec &trackVec) const
{
    for (unsigned int iTrack = 0, nTracks = trackVec.size(); iTrack < nTracks; ++iTrack)
    {
        Track *pTrack = trackVec[iTrack];

        if (this->IsDaughter(pTrack) || this->IsParent(pTrack) || this->IsV0(pTrack))
            return true;
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------------------------------

StatusCode TrackCreator::CreateTracks(const LCEvent *const pLCEvent) const
{
    // Insert user code here ...
    static pandora::Pandora *pPandora = PandoraPFANewProcessor::GetPandora();
    static const gear::GearParameters &ftdParameters = marlin::Global::GEAR->getGearParameters("FTD");
    static const DoubleVector ftdInnerRadii(ftdParameters.getDoubleVals("FTDInnerRadius"));
    static const DoubleVector ftdOuterRadii(ftdParameters.getDoubleVals("FTDOuterRadius"));
    static const DoubleVector ftdZPositions(ftdParameters.getDoubleVals("FTDZCoordinate"));
    static const unsigned int nFtdLayers(ftdZPositions.size());
    static const float tanLambdaFtd = ftdZPositions[0]/ftdOuterRadii[0];
    
    for (StringVector::const_iterator iter = m_settings.m_trackCollections.begin(), iterEnd = m_settings.m_trackCollections.end();
        iter != iterEnd; ++iter)
    {
        try
        {
            const LCCollection *pTrackCollection = pLCEvent->getCollection(*iter);

            for (int i = 0, iMax = pTrackCollection->getNumberOfElements(); i < iMax; ++i)
            {
                try
                {
                    Track *pTrack = dynamic_cast<Track*>(pTrackCollection->getElementAt(i));

		    int minTrackHits = m_settings.m_minTrackHits; 
		    float tanLambda = fabs(pTrack->getTanLambda()); 
		    if(tanLambda>tanLambdaFtd)
		    {
		        int expectedFtdHits(0);
		        for(unsigned int iFtdLayer = 0; iFtdLayer< nFtdLayers; iFtdLayer++)
                        {
			    if( (tanLambda > ftdZPositions[iFtdLayer]/ftdOuterRadii[iFtdLayer]  ) &&
			        (tanLambda < ftdZPositions[iFtdLayer]/ftdInnerRadii[iFtdLayer]  ) )expectedFtdHits++;
			}
			minTrackHits = std::max(m_settings.m_minFtdTrackHits,expectedFtdHits); 
		    }

                    const int nTrackHits(static_cast<int>(pTrack->getTrackerHits().size()));


                    if ((nTrackHits < minTrackHits) || (nTrackHits > m_settings.m_maxTrackHits))
                        continue;

                    // Proceed to create the pandora track
                    PandoraApi::Track::Parameters trackParameters;
                    trackParameters.m_d0 = pTrack->getD0();
                    trackParameters.m_z0 = pTrack->getZ0();
                    trackParameters.m_pParentAddress = pTrack;

                    // By default, assume tracks are charged pions
                    const float signedCurvature(pTrack->getOmega());
                    trackParameters.m_particleId = (signedCurvature > 0) ? pandora::PI_PLUS : pandora::PI_MINUS;
                    trackParameters.m_mass = pandora::PdgTable::GetParticleMass(pandora::PI_PLUS);

                    // Use particle id information from V0 and Kink finders
                    TrackToPidMap::const_iterator iter = m_trackToPidMap.find(pTrack);

                    if(iter != m_trackToPidMap.end())
                    {
                        trackParameters.m_particleId = (*iter).second;
                        trackParameters.m_mass = pandora::PdgTable::GetParticleMass((*iter).second);
                    }

                    if (0. != signedCurvature)
                        trackParameters.m_charge = static_cast<int>(signedCurvature / std::fabs(signedCurvature));

                    this->FitTrackHelices(pTrack, trackParameters);
                    this->TrackReachesECAL(pTrack, trackParameters);
                    this->DefineTrackPfoUsage(pTrack, trackParameters);

                    PANDORA_THROW_RESULT_IF(STATUS_CODE_SUCCESS, !=, PandoraApi::Track::Create(*pPandora, trackParameters));
                    m_trackVector.push_back(pTrack);
                }
                catch (StatusCodeException &statusCodeException)
                {
                    streamlog_out(ERROR) << "Failed to extract a track: " << statusCodeException.ToString() << std::endl;
                }
                catch (...)
                {
                    streamlog_out(WARNING) << "Failed to extract a track, unrecognised exception" << std::endl;
                }
            }
        }
        catch (...)
        {
            streamlog_out(MESSAGE) << "Failed to extract track collection: " << *iter << std::endl;
        }
    }

    return STATUS_CODE_SUCCESS;
}

//------------------------------------------------------------------------------------------------------------------------------------------

void TrackCreator::FitTrackHelices(const Track *const pTrack, PandoraApi::Track::Parameters &trackParameters) const
{
    static const float bField(marlin::Global::GEAR->getBField().at(gear::Vector3D(0., 0., 0.)).z());

    // Fit from track parameters to determine momentum at dca
    HelixClass *pHelixFit = new HelixClass();
    pHelixFit->Initialize_Canonical(pTrack->getPhi(), pTrack->getD0(), pTrack->getZ0(), pTrack->getOmega(), pTrack->getTanLambda(), bField);
    trackParameters.m_momentumAtDca = pandora::CartesianVector(pHelixFit->getMomentum()[0], pHelixFit->getMomentum()[1], pHelixFit->getMomentum()[2]);

    // Fit start and end of tracks
    TrackerHitVec trackerHitvec(pTrack->getTrackerHits());
    const int nTrackHits = trackerHitvec.size();
    const int nTrackHitsForFit = std::min(m_settings.m_nHitsForHelixFits, nTrackHits);

    // Order hits by increasing z
    for (int iz = 0 ; iz < nTrackHits - 1; ++iz)
    {
        for (int jz = 0; jz < nTrackHits - iz - 1; ++jz)
        {
            if (trackerHitvec[jz]->getPosition()[2] > trackerHitvec[jz + 1]->getPosition()[2])
            {
                TrackerHit *pTempTrackerHit = trackerHitvec[jz];
                trackerHitvec[jz] = trackerHitvec[jz + 1];
                trackerHitvec[jz + 1] = pTempTrackerHit;
            }
        }
    }

    const float zMin(trackerHitvec[0]->getPosition()[2]);
    const float zMax(trackerHitvec[nTrackHits - 1]->getPosition()[2]);
    const int signPz(std::fabs(zMin) < std::fabs(zMax) ? 1 : -1);

    // Arrays for helix fits
    float xf[nTrackHitsForFit], yf[nTrackHitsForFit], zf[nTrackHitsForFit], af[nTrackHitsForFit];
    float xb[nTrackHitsForFit], yb[nTrackHitsForFit], zb[nTrackHitsForFit], ab[nTrackHitsForFit];

    for (int i = 0; i < nTrackHitsForFit; ++i)
    {
        xf[i] = trackerHitvec[i]->getPosition()[0];
        yf[i] = trackerHitvec[i]->getPosition()[1];
        zf[i] = trackerHitvec[i]->getPosition()[2];
        af[i] = 0;

        int j = nTrackHits - 1 - i;
        xb[i] = trackerHitvec[j]->getPosition()[0];
        yb[i] = trackerHitvec[j]->getPosition()[1];
        zb[i] = trackerHitvec[j]->getPosition()[2];
        ab[i] = 0;
    }

    // Helix from first nTrackHitsForFit (i.e. lowest z)
    float par[5], dpar[5], chi2, distmax;
    ClusterShapes clusterShapesF(nTrackHitsForFit, af, xf, yf, zf);
    clusterShapesF.FitHelix(500, 0, 1, par, dpar, chi2, distmax);
    HelixClass *pHelix1 = new HelixClass();
    pHelix1->Initialize_BZ(par[0], par[1], par[2], par[3], par[4], bField, signPz, zMin);

    // Helix from last nTrackHitsForFit (i.e. highest z)
    ClusterShapes clusterShapesB(nTrackHitsForFit, ab, xb, yb, zb);
    clusterShapesB.FitHelix(500, 0, 1, par, dpar, chi2, distmax);
    HelixClass *pHelix2 = new HelixClass();
    pHelix2->Initialize_BZ(par[0], par[1], par[2], par[3], par[4], bField, signPz, zMax);

    // Label as start and end depending on assigned sign of Pz
    HelixClass *const pHelixStart = (signPz > 0) ? pHelix1 : pHelix2;
    HelixClass *const pHelixEnd   = (signPz > 0) ? pHelix2 : pHelix1;

    trackParameters.m_trackStateAtStart = pandora::TrackState(pHelixStart->getReferencePoint()[0], pHelixStart->getReferencePoint()[1],
        pHelixStart->getReferencePoint()[2], pHelixStart->getMomentum()[0], pHelixStart->getMomentum()[1], pHelixStart->getMomentum()[2]);

    trackParameters.m_trackStateAtEnd = pandora::TrackState(pHelixEnd->getReferencePoint()[0], pHelixEnd->getReferencePoint()[1],
        pHelixEnd->getReferencePoint()[2], pHelixEnd->getMomentum()[0], pHelixEnd->getMomentum()[1], pHelixEnd->getMomentum()[2]);

    // Get track state at ecal surface
    HelixClass* pHelixToProject = pHelixFit;

    if(0 != m_settings.m_useEndTrackHelixForECalProjection)
        pHelixToProject = pHelixEnd;

    float referencePoint[3] = {pHelixToProject->getReferencePoint()[0], pHelixToProject->getReferencePoint()[1],
        pHelixToProject->getReferencePoint()[2]};

    trackParameters.m_trackStateAtECal = this->GetECalProjection(pHelixToProject, referencePoint, signPz);

    streamlog_out(DEBUG) << "TrackStateAtStart: " << std::endl << trackParameters.m_trackStateAtStart.Get() << std::endl
                         << "TrackStateAtEnd: "   << std::endl << trackParameters.m_trackStateAtEnd.Get()   << std::endl
                         << "TrackStateAtECal: "  << std::endl << trackParameters.m_trackStateAtECal.Get()  << std::endl;

    delete pHelix1;
    delete pHelix2;
    delete pHelixFit;
}

//------------------------------------------------------------------------------------------------------------------------------------------

void TrackCreator::TrackReachesECAL(const Track *const pTrack, PandoraApi::Track::Parameters &trackParameters) const
{
    try
    {
        // Extract tracking subdetector parameters
        static const gear::TPCParameters &tpcParameters = marlin::Global::GEAR->getTPCParameters();
        static const float tpcInnerR(tpcParameters.getPadLayout().getPlaneExtent()[0]);
        static const float tpcOuterR(tpcParameters.getPadLayout().getPlaneExtent()[1]);
        static const float tpcZmax(tpcParameters.getMaxDriftLength());

        static const gear::GearParameters &ftdParameters = marlin::Global::GEAR->getGearParameters("FTD");
        static const DoubleVector ftdInnerRadii(ftdParameters.getDoubleVals("FTDInnerRadius"));
        static const DoubleVector ftdOuterRadii(ftdParameters.getDoubleVals("FTDOuterRadius"));
        static const DoubleVector ftdZPositions(ftdParameters.getDoubleVals("FTDZCoordinate"));
        static const unsigned int nFtdLayers(ftdZPositions.size());

        static const gear::GearParameters &etdParameters = marlin::Global::GEAR->getGearParameters("ETD");
        static const DoubleVector etdZPositions(etdParameters.getDoubleVals("ETDLayerZ"));
        static const gear::GearParameters &setParameters = marlin::Global::GEAR->getGearParameters("SET");
        static const DoubleVector setInnerRadii(setParameters.getDoubleVals("SETLayerRadius"));

        // First pass validation of tracking geometry
        static bool isFirstPass(true);

        if (isFirstPass)
        {
            isFirstPass = false;

            if ((0 == nFtdLayers) || etdZPositions.empty() || setInnerRadii.empty())
                throw StatusCodeException(STATUS_CODE_INVALID_PARAMETER);

            if ((nFtdLayers != ftdInnerRadii.size()) || (nFtdLayers != ftdOuterRadii.size()))
                throw StatusCodeException(STATUS_CODE_INVALID_PARAMETER);
        }

        static const double minEtdZPosition(*(std::min_element(etdZPositions.begin(), etdZPositions.end())));
        static const double minSetRadius(*(std::min_element(setInnerRadii.begin(), setInnerRadii.end())));

        // Calculate hit position information
        float hitZMin(std::numeric_limits<float>::max());
        float hitZMax(-std::numeric_limits<float>::max());
        float hitOuterR(-std::numeric_limits<float>::max());

        int nTpcHits(0);
        int nFtdHits(0);

        const TrackerHitVec &trackerHitVec(pTrack->getTrackerHits());
        const unsigned int nTrackHits(trackerHitVec.size());

        for (unsigned int i = 0; i < nTrackHits; ++i)
        {
            const float x(static_cast<float>(trackerHitVec[i]->getPosition()[0]));
            const float y(static_cast<float>(trackerHitVec[i]->getPosition()[1]));
            const float z(static_cast<float>(trackerHitVec[i]->getPosition()[2]));
            const float r(std::sqrt(x * x + y * y));

            if (z > hitZMax)
                hitZMax = z;

            if (z < hitZMin)
                hitZMin = z;

            if (r > hitOuterR)
                hitOuterR = r;

            if (r > tpcInnerR)
            {
                nTpcHits++;
                continue;
            }

            for (unsigned int j = 0; j < nFtdLayers; ++j)
            {
                if ((r > ftdInnerRadii[j]) && (r < ftdOuterRadii[j]) &&
                    (std::fabs(z) - m_settings.m_reachesECalFtdZMaxDistance < ftdZPositions[j]) &&
                    (std::fabs(z) + m_settings.m_reachesECalFtdZMaxDistance > ftdZPositions[j]))
                {
                    nFtdHits++;
                    break;
                }
            }
        }

        // Look to see if there are hits in etd or set, implying track has reached edge of ecal
        if ((hitOuterR > minSetRadius) || (hitZMax > minEtdZPosition))
        {
            trackParameters.m_reachesECal = true;
            return;
        }

        // Require sufficient hits in tpc or ftd, then compare extremal hit positions with tracker dimensions
        if ((nTpcHits >= m_settings.m_reachesECalNTpcHits) || (nFtdHits >= m_settings.m_reachesECalNFtdHits))
        {
            if ((hitOuterR - tpcOuterR > m_settings.m_reachesECalTpcOuterDistance) ||
                (std::fabs(hitZMax) - tpcZmax > m_settings.m_reachesECalTpcZMaxDistance) ||
                (std::fabs(hitZMin) - tpcZmax > m_settings.m_reachesECalTpcZMaxDistance))
            {
                trackParameters.m_reachesECal = true;
                return;
            }
        }

        // If track is lowpt, it may curl up and end inside tpc inner radius
        static const float bField(marlin::Global::GEAR->getBField().at(gear::Vector3D(0., 0., 0.)).z());
        static const float cosTpc(tpcZmax / std::sqrt(tpcZmax * tpcZmax + tpcInnerR * tpcInnerR));

        const pandora::CartesianVector &momentumAtDca(trackParameters.m_momentumAtDca.Get());
        const float cosAngleAtDca(std::fabs(momentumAtDca.GetZ()) / momentumAtDca.GetMagnitude());
        const float pX(momentumAtDca.GetX()), pY(momentumAtDca.GetY());
        const float pT(std::sqrt(pX * pX + pY * pY));

        if ((cosAngleAtDca > cosTpc) || (pT < m_settings.m_curvatureToMomentumFactor * bField * tpcOuterR))
        {
            trackParameters.m_reachesECal = true;
            return;
        }

        trackParameters.m_reachesECal = false;
    }
    catch (...)
    {
        streamlog_out(ERROR) << "TrackCreator::TrackReachesECAL - Failed to extract tracking subdetector parameters. " << std::endl;
        throw StatusCodeException(STATUS_CODE_INVALID_PARAMETER);
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------

void TrackCreator::DefineTrackPfoUsage(const Track *const pTrack, PandoraApi::Track::Parameters &trackParameters) const
{
    bool canFormPfo(false);
    bool canFormClusterlessPfo(false);

    if (trackParameters.m_reachesECal.Get() && !this->IsParent(pTrack))
    {
        const float d0(std::fabs(pTrack->getD0())), z0(std::fabs(pTrack->getZ0()));

        TrackerHitVec trackerHitvec(pTrack->getTrackerHits());
        float rInner(std::numeric_limits<float>::max()), zMin(std::numeric_limits<float>::max());

        for (TrackerHitVec::const_iterator iter = trackerHitvec.begin(), iterEnd = trackerHitvec.end(); iter != iterEnd; ++iter)
        {
            const double *pPosition((*iter)->getPosition());
            const float x(pPosition[0]), y(pPosition[1]), absoluteZ(std::fabs(pPosition[2]));
            const float r(std::sqrt(x * x + y * y));

            if (r < rInner)
                rInner = r;

            if (absoluteZ < zMin)
                zMin = absoluteZ;
        }

        if (this->PassesQualityCuts(pTrack, trackParameters, rInner))
        {
            static const float tpcInnerR(marlin::Global::GEAR->getTPCParameters().getPadLayout().getPlaneExtent()[0]);

            const pandora::CartesianVector &momentumAtDca(trackParameters.m_momentumAtDca.Get());
            const float pX(momentumAtDca.GetX()), pY(momentumAtDca.GetY()), pZ(momentumAtDca.GetZ());
            const float pT(std::sqrt(pX * pX + pY * pY));

            const float zCutForNonVertexTracks(tpcInnerR * std::fabs(pZ / pT) + m_settings.m_zCutForNonVertexTracks);
            const bool passRzQualityCuts((zMin < zCutForNonVertexTracks) && (rInner < tpcInnerR + m_settings.m_maxTpcInnerRDistance));

            const bool isV0(this->IsV0(pTrack));
            const bool isDaughter(this->IsDaughter(pTrack));

            // Decide whether track can be associated with a pandora cluster and used to form a charged PFO
            if ((d0 < m_settings.m_d0TrackCut) && (z0 < m_settings.m_z0TrackCut) && (rInner < tpcInnerR + m_settings.m_maxTpcInnerRDistance))
            {
                canFormPfo = true;
            }
            else if (passRzQualityCuts && (0 != m_settings.m_usingNonVertexTracks))
            {
                canFormPfo = true;
            }
            else if (isV0 || isDaughter)
            {
                canFormPfo = true;
            }

            // Decide whether track can be used to form a charged PFO, even if track fails to be associated with a pandora cluster
            const float particleMass(trackParameters.m_mass.Get());
            const float trackEnergy(std::sqrt(momentumAtDca.GetMagnitudeSquared() + particleMass * particleMass));

            if ((0 != m_settings.m_usingUnmatchedVertexTracks) && (trackEnergy < m_settings.m_unmatchedVertexTrackMaxEnergy))
            {
                if ((d0 < m_settings.m_d0UnmatchedVertexTrackCut) && (z0 < m_settings.m_z0UnmatchedVertexTrackCut) &&
                    (rInner < tpcInnerR + m_settings.m_maxTpcInnerRDistance))
                {
                    canFormClusterlessPfo = true;
                }
                else if (passRzQualityCuts && (0 != m_settings.m_usingNonVertexTracks) && (0 != m_settings.m_usingUnmatchedNonVertexTracks))
                {
                    canFormClusterlessPfo = true;
                }
                else if (isV0 || isDaughter)
                {
                    canFormClusterlessPfo = true;
                }
            }
        }
    }

    trackParameters.m_canFormPfo = canFormPfo;
    trackParameters.m_canFormClusterlessPfo = canFormClusterlessPfo;
}

//------------------------------------------------------------------------------------------------------------------------------------------

pandora::TrackState TrackCreator::GetECalProjection(HelixClass *const pHelix, float referencePoint[3], int signPz) const
{
    static const gear::CalorimeterParameters &ecalBarrelParameters = marlin::Global::GEAR->getEcalBarrelParameters();
    static const gear::CalorimeterParameters &ecalEndCapParameters = marlin::Global::GEAR->getEcalEndcapParameters();

    static const float phi0(ecalBarrelParameters.getPhi0());
    static const int ecalSymmetryOrder(ecalBarrelParameters.getSymmetryOrder());
    static const float rOfBarrel(ecalBarrelParameters.getExtent()[0]);
    static const float zOfEndCap(ecalEndCapParameters.getExtent()[2]);

    float bestEcalProjection[3];

    // First project to endcap
    float minTime = pHelix->getPointInZ(static_cast<float>(signPz) * zOfEndCap, referencePoint, bestEcalProjection);

    // Then project to barrel surface(s)
    float barrelProjection[3];
    static const float pi(std::acos(-1.));

    if (ecalSymmetryOrder > 0)
    {
        // Polygon
        float twopi_n = 2. * pi / (static_cast<float>(ecalSymmetryOrder));

        for (int i = 0; i < ecalSymmetryOrder; ++i)
        {
            float phi = twopi_n * static_cast<float>(i) + phi0;
            float xx = rOfBarrel * cos(phi);
            float yy = rOfBarrel * sin(phi);
            float ax = cos(phi + 0.5*pi);
            float ay = sin(phi + 0.5*pi);
            float tt = pHelix->getPointInXY(xx, yy , ax, ay, referencePoint, barrelProjection);

            // If helix intersects this plane before current best use this point
            if (tt < minTime)
            {
                minTime = tt;
                bestEcalProjection[0] = barrelProjection[0];
                bestEcalProjection[1] = barrelProjection[1];
                bestEcalProjection[2] = barrelProjection[2];
            }
        }
    }
    else
    {
        // Cylinder
        float tt = pHelix->getPointOnCircle(rOfBarrel, referencePoint, barrelProjection);

        if (tt < minTime)
        {
            minTime = tt;
            bestEcalProjection[0] = barrelProjection[0];
            bestEcalProjection[1] = barrelProjection[1];
            bestEcalProjection[2] = barrelProjection[2];
        }
    }

    float extrapolatedMomentum[3];
    pHelix->getExtrapolatedMomentum(bestEcalProjection, extrapolatedMomentum);

    return pandora::TrackState(bestEcalProjection[0], bestEcalProjection[1], bestEcalProjection[2],
        extrapolatedMomentum[0], extrapolatedMomentum[1], extrapolatedMomentum[2]);
}

//------------------------------------------------------------------------------------------------------------------------------------------

bool TrackCreator::PassesQualityCuts(const Track *const pTrack, const PandoraApi::Track::Parameters &trackParameters, const float rInner) const
{
    // ATTN Used to contain cuts on track chi2 values and energies. Reduced to simple sanity check for first official release.
    if (trackParameters.m_trackStateAtECal.Get().GetPosition().GetMagnitude() < m_settings.m_minTrackECalDistanceFromIp)
        return false;

    return true;
}
