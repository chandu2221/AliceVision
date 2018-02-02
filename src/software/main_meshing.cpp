// This file is part of the AliceVision project.
// This Source Code Form is subject to the terms of the Mozilla Public License,
// v. 2.0. If a copy of the MPL was not distributed with this file,
// You can obtain one at https://mozilla.org/MPL/2.0/.

#include <aliceVision/delaunaycut/mv_delaunay_GC.hpp>
#include <aliceVision/delaunaycut/mv_delaunay_meshSmooth.hpp>
#include <aliceVision/largeScale/reconstructionPlan.hpp>
#include <aliceVision/planeSweeping/ps_refine_rc.hpp>
#include <aliceVision/CUDAInterfaces/refine.hpp>
#include <aliceVision/common/fileIO.hpp>

#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>


namespace bfs = boost::filesystem;
namespace po = boost::program_options;

#define ALICEVISION_COUT(x) std::cout << x << std::endl
#define ALICEVISION_CERR(x) std::cerr << x << std::endl


enum EPartitioning {
    eUndefined = 0,
    eSingleBlock = 1,
    eAuto = 2
};

EPartitioning EPartitioning_stringToEnum(const std::string& s)
{
    if(s == "singleBlock")
        return eSingleBlock;
    if(s == "auto")
        return eAuto;
    return eUndefined;
}

inline std::istream& operator>>(std::istream& in, EPartitioning& mode)
{
    std::string s;
    in >> s;
    mode = EPartitioning_stringToEnum(s);
    return in;
}


int main(int argc, char* argv[])
{
    long startTime = clock();

    std::string iniFilepath;
    std::string outputMesh;
    std::string depthMapFolder;
    std::string depthMapFilterFolder;
    EPartitioning partitioning = eSingleBlock;
    po::options_description inputParams;
    int maxPts = 6000000;
    int maxPtsPerVoxel = 6000000;

    po::options_description allParams("AliceVision meshing");

    po::options_description requiredParams("Required parameters");
    requiredParams.add_options()
        ("ini", po::value<std::string>(&iniFilepath)->required(),
            "Configuration file (mvs.ini).")
        ("depthMapFolder", po::value<std::string>(&depthMapFolder)->required(),
            "Input depth maps folder.")
        ("depthMapFilterFolder", po::value<std::string>(&depthMapFilterFolder)->required(),
            "Input filtered depth maps folder.")
        ("output,o", po::value<std::string>(&outputMesh)->required(),
            "Output mesh (OBJ file format).");

    po::options_description optionalParams("Optional parameters");
    optionalParams.add_options()
        ("maxPts", po::value<int>(&maxPts)->default_value(maxPts),
            "Max points.")
        ("maxPtsPerVoxel", po::value<int>(&maxPtsPerVoxel)->default_value(maxPtsPerVoxel),
            "Max points per voxel.")
        ("partitioning", po::value<EPartitioning>(&partitioning)->default_value(partitioning),
            "Partitioning: singleBlock or auto.");

    allParams.add(requiredParams).add(optionalParams);

    po::variables_map vm;

    try
    {
      po::store(po::parse_command_line(argc, argv, allParams), vm);

      if(vm.count("help") || (argc == 1))
      {
        ALICEVISION_COUT(allParams);
        return EXIT_SUCCESS;
      }

      po::notify(vm);
    }
    catch(boost::program_options::required_option& e)
    {
      ALICEVISION_CERR("ERROR: " << e.what() << std::endl);
      ALICEVISION_COUT("Usage:\n\n" << allParams);
      return EXIT_FAILURE;
    }
    catch(boost::program_options::error& e)
    {
      ALICEVISION_CERR("ERROR: " << e.what() << std::endl);
      ALICEVISION_COUT("Usage:\n\n" << allParams);
      return EXIT_FAILURE;
    }

    ALICEVISION_COUT("ini file: " << iniFilepath);

    // .ini parsing
    multiviewInputParams mip(iniFilepath, depthMapFolder, depthMapFilterFolder);
    const double simThr = mip._ini.get<double>("global.simThr", 0.0);
    multiviewParams mp(mip.getNbCameras(), &mip, (float) simThr);
    mv_prematch_cams pc(&mp);

    // .ini parsing
    int ocTreeDim = mip._ini.get<int>("largeScale.gridLevel0", 1024);
    const auto baseDir = mip._ini.get<std::string>("largeScale.baseDirName", "root01024");

    bfs::path outDirectory = bfs::path(outputMesh).parent_path();
    if(!bfs::is_directory(outDirectory))
        bfs::create_directory(outDirectory);

    bfs::path tmpDirectory = outDirectory / "tmp";

    switch(partitioning)
    {
        case eAuto:
        {
            ALICEVISION_COUT("--- meshing partitioning: auto");
            largeScale lsbase(&mp, &pc, tmpDirectory.string() + "/");
            lsbase.generateSpace(maxPtsPerVoxel, ocTreeDim);
            std::string voxelsArrayFileName = lsbase.spaceFolderName + "hexahsToReconstruct.bin";
            staticVector<point3d>* voxelsArray = nullptr;
            if(FileExists(voxelsArrayFileName))
            {
                // If already computed reload it.
                ALICEVISION_COUT("Voxels array already computed, reload from file: " << voxelsArrayFileName);
                voxelsArray = loadArrayFromFile<point3d>(voxelsArrayFileName);
            }
            else
            {
                ALICEVISION_COUT("Compute voxels array");
                reconstructionPlan rp(lsbase.dimensions, &lsbase.space[0], lsbase.mp, lsbase.pc, lsbase.spaceVoxelsFolderName);
                voxelsArray = rp.computeReconstructionPlanBinSearch(maxPts);
                saveArrayToFile<point3d>(voxelsArrayFileName, voxelsArray);
            }
            reconstructSpaceAccordingToVoxelsArray(voxelsArrayFileName, &lsbase, true);
            // Join meshes
            mv_mesh* mesh = joinMeshes(voxelsArrayFileName, &lsbase);

            ALICEVISION_COUT("Saving joined meshes");

            bfs::path spaceBinFileName = outDirectory/"denseReconstruction.bin";
            mesh->saveToBin(spaceBinFileName.string());

            // Export joined mesh to obj
            mv_output3D o3d(lsbase.mp);
            o3d.saveMvMeshToObj(mesh, outputMesh);

            delete mesh;

            // Join ptsCams
            staticVector<staticVector<int>*>* ptsCams = loadLargeScalePtsCams(lsbase.getRecsDirs(voxelsArray));
            saveArrayOfArraysToFile<int>((outDirectory/"meshPtsCamsFromDGC.bin").string(), ptsCams);
            deleteArrayOfArrays<int>(&ptsCams);
        }
        case eSingleBlock:
        {
            ALICEVISION_COUT("--- meshing partitioning: single block");
            largeScale ls0(&mp, &pc, tmpDirectory.string() + "/");
            ls0.generateSpace(maxPtsPerVoxel, ocTreeDim);
            unsigned long ntracks = std::numeric_limits<unsigned long>::max();
            while(ntracks > maxPts)
            {
                bfs::path dirName = outDirectory/("largeScaleMaxPts" + num2strFourDecimal(ocTreeDim));
                largeScale* ls = ls0.cloneSpaceIfDoesNotExists(ocTreeDim, dirName.string() + "/");
                voxelsGrid vg(ls->dimensions, &ls->space[0], ls->mp, ls->pc, ls->spaceVoxelsFolderName);
                ntracks = vg.getNTracks();
                delete ls;
                ALICEVISION_COUT("Number of track candidates: " << ntracks);
                if(ntracks > maxPts)
                {
                    ALICEVISION_COUT("ocTreeDim: " << ocTreeDim);
                    double t = (double)ntracks / (double)maxPts;
                    ALICEVISION_COUT("downsample: " << ((t < 2.0) ? "slow" : "fast"));
                    ocTreeDim = (t < 2.0) ? ocTreeDim-100 : ocTreeDim*0.5;
                }
            }
            ALICEVISION_COUT("Number of tracks: " << ntracks);
            ALICEVISION_COUT("ocTreeDim: " << ocTreeDim);
            bfs::path dirName = outDirectory/("largeScaleMaxPts" + num2strFourDecimal(ocTreeDim));
            largeScale lsbase(&mp, &pc, dirName.string()+"/");
            lsbase.loadSpaceFromFile();
            reconstructionPlan rp(lsbase.dimensions, &lsbase.space[0], lsbase.mp, lsbase.pc, lsbase.spaceVoxelsFolderName);
            staticVector<int> voxelNeighs(rp.voxels->size() / 8);
            for(int i = 0; i < rp.voxels->size() / 8; i++)
                voxelNeighs.push_back(i);
            mv_delaunay_GC delaunayGC(lsbase.mp, lsbase.pc);
            staticVector<point3d>* hexahsToExcludeFromResultingMesh = nullptr;
            point3d* hexah = &lsbase.space[0];
            delaunayGC.reconstructVoxel(hexah, &voxelNeighs, outDirectory.string()+"/", lsbase.getSpaceCamsTracksDir(), false, hexahsToExcludeFromResultingMesh,
                                  (voxelsGrid*)&rp, lsbase.getSpaceSteps());

            bool exportDebugGC = mip._ini.get<bool>("delaunaycut.exportDebugGC", false);
            //if(exportDebugGC)
            //    delaunayGC.saveMeshColoredByCamsConsistency((outDirectory/"meshColoredbyCamsConsistency.wrl").string(),
            //                                                (outDirectory/"meshColoredByVisibility.wrl").string());

            delaunayGC.graphCutPostProcessing();
            if(exportDebugGC)
                delaunayGC.saveMeshColoredByCamsConsistency((outDirectory/"meshColoredbyCamsConsistency_postprocess.wrl").string(),
                                                            (outDirectory/"meshColoredByVisibility_postprocess.wrl").string());

            // Save mesh as .bin and .obj
            mv_mesh* mesh = delaunayGC.createMesh();
            if(mesh->pts->empty())
              throw std::runtime_error("Empty mesh");

            staticVector<staticVector<int>*>* ptsCams = delaunayGC.createPtsCams();
            staticVector<int> usedCams = delaunayGC.getSortedUsedCams();

            meshPostProcessing(mesh, ptsCams, usedCams, mp, pc, outDirectory.string()+"/", hexahsToExcludeFromResultingMesh, hexah);
            mesh->saveToBin((outDirectory/"denseReconstruction.bin").string());

            saveArrayOfArraysToFile<int>((outDirectory/"meshPtsCamsFromDGC.bin").string(), ptsCams);
            deleteArrayOfArrays<int>(&ptsCams);

            mv_output3D o3d(&mp);
            o3d.saveMvMeshToObj(mesh, outputMesh);

            delete mesh;
        }
        case eUndefined:
            throw std::invalid_argument("Partitioning not defined");
    }

    printfElapsedTime(startTime, "#");
    return EXIT_SUCCESS;
}
