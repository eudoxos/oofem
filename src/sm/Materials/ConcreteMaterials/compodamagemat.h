/*
 *
 *                 #####    #####   ######  ######  ###   ###
 *               ##   ##  ##   ##  ##      ##      ## ### ##
 *              ##   ##  ##   ##  ####    ####    ##  #  ##
 *             ##   ##  ##   ##  ##      ##      ##     ##
 *            ##   ##  ##   ##  ##      ##      ##     ##
 *            #####    #####   ##      ######  ##     ##
 *
 *
 *             OOFEM : Object Oriented Finite Element Code
 *
 *               Copyright (C) 1993 - 2013   Borek Patzak
 *
 *
 *
 *       Czech Technical University, Faculty of Civil Engineering,
 *   Department of Structural Mechanics, 166 29 Prague, Czech Republic
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef compodamagemat_h
#define compodamagemat_h

#include "material.h"
#include "sm/Materials/linearelasticmaterial.h"
#include "sm/Materials/structuralmaterial.h"
#include "sm/Materials/structuralms.h"
#include "intarray.h"
#include "floatarray.h"
#include "cltypes.h"

///@name Input fields for CompoDamageMat
//@{
#define _IFT_CompoDamageMat_Name "compdammat"
#define _IFT_CompoDamageMat_exx "exx"
#define _IFT_CompoDamageMat_eyyezz "eyyezz"
#define _IFT_CompoDamageMat_nuxynuxz "nuxynuxz"
#define _IFT_CompoDamageMat_nuyz "nuyz"
#define _IFT_CompoDamageMat_Gxy "gxygxz"
#define _IFT_CompoDamageMat_tension_f0_gf "tension_f0_gf"
#define _IFT_CompoDamageMat_compres_f0_gf "compres_f0_gf"
#define _IFT_CompoDamageMat_afteriter "afteriter"
#define _IFT_CompoDamageMat_allowSnapBack "allowsnapback"
//@}

namespace oofem {
/**
 * Class for maintaining Gauss point values for CompoDamageMat model.
 * Prefix temp* refers to unequilibrated values, e.g. tempOmega is a temporal damage array
 *
 * @author Vit Smilauer
 */
class CompoDamageMatStatus : public StructuralMaterialStatus
{
public:
    /// Constructor
    CompoDamageMatStatus(GaussPoint * g);

    void printOutputAt(FILE *file, TimeStep *tStep) const override;

    void initTempStatus() override;

    void updateYourself(TimeStep *tStep) override;

    //tempVal are values used during iteration, Val are equilibrated values, updated after last iteration in previous time step

    /// Highest strain ever reached at IP. Can be unequilibrated from last iterations [6 tension, 6 compression]
    FloatArray tempKappa;

    /// Highest strain ever reached in all previous equilibrated steps [6 tension, 6 compression]
    FloatArray kappa;

    /// Highest damage ever reached at IP. Can be unequilibrated from last iterations [6 for tension and compression]
    FloatArray tempOmega;

    /// Checks whether snapback occurred at IP.
    IntArray hasSnapBack;

    /// Highest damage ever reached in all previous equilibrated steps at IP [6 for tension and compression]
    FloatArray omega;

    /// Iteration in the time step
    int Iteration = 0;

    /// Stress at which damage starts. For uniaxial loading is equal to given maximum stress in the input. The stress is linearly interpolated between increments at IP [6 tension, 6 compression]
    FloatArray initDamageStress;
    /// Strain when damage is initiated at IP. In literature denoted eps_0 [6 tension, 6 compression]
    FloatArray strainAtMaxStress;
    /// Maximum strain when stress becomes zero due to complete damage (omega = 1) at IP. Determined from fracture energy and characteristic length. Derived for mode I only on the uniaxial loading case. In literature denoted often eps_f [6 tension, 6 compression]
    FloatArray maxStrainAtZeroStress;

    /// Only for printing purposes in CompoDamageMatStatus
    FloatArray tempStressMLCS;

    /// Characteristic element length at IP in three perpendicular planes aligned with material orientation
    FloatArray elemCharLength;

    void saveContext(DataStream &stream, ContextMode mode) override;
    void restoreContext(DataStream &stream, ContextMode mode) override;

    const char *giveClassName() const override { return "CompoDamageMatStatus"; }
};



/**
 * Material damage model for transversely orthotropic material. Fixed cracks are induced in principal material coordinates, always perpendicular to material axis.
 * Six cracking modes are implemented in three orthogonal directions - three for tension/compression failure and three for shear failure.
 * Material orientation can be specified on each element with "lmcs" keyword, otherwise global material orientation is assumed.
 *
 * Evolution of cracks is determined separately in compression and tension. Linear softening is assumed (can be easily changed to exponential but requires then inner iterations)
 *
 * For derivation of the model see the book Bazant and Planas, Fracture and Size Effect in Concrete and Other Quasibrittle Materials, pp.236 or
 * article Bazant and Oh: Crack band theory for fracture of concrete, Materials and Structures, 1983.
 *
 * The model is aimed for 3D problems but extension for 1D truss works.
 * In this particular case, only the first array component is used in all involved variables.
 *
 * @author Vit Smilauer
 *
 * @todo This class should overload give1dMaterialStiffMtrx
 */
class CompoDamageMat : public StructuralMaterial
{
public:
    /// Constructor
    CompoDamageMat(int n, Domain * d);

    const char *giveClassName() const override { return "CompositeDamageMaterial"; }
    const char *giveInputRecordName() const override { return _IFT_CompoDamageMat_Name; }

    void initializeFrom(InputRecord &ir) override;
    void giveInputRecord(DynamicInputRecord &input) override;

    std::unique_ptr<MaterialStatus> CreateStatus(GaussPoint *gp) const override { return std::make_unique<CompoDamageMatStatus>(gp); }

    FloatMatrixF<6,6> give3dMaterialStiffnessMatrix(MatResponseMode mmode, GaussPoint *gp, TimeStep *tStep) const override;

    void giveRealStressVector(FloatArray &answer, GaussPoint *gp,
                              const FloatArray &, TimeStep *tStep) const override;

    FloatArrayF<6> giveRealStressVector_3d(const FloatArrayF<6> &strain, GaussPoint *gp, TimeStep *tStep) const override
    {
        FloatArray answer;
        const_cast<CompoDamageMat*>(this)->giveRealStressVector(answer, gp, strain, tStep);
        return answer;
    }
    FloatArrayF<1> giveRealStressVector_1d(const FloatArrayF<1> &strain, GaussPoint *gp, TimeStep *tStep) const override
    {
        FloatArray answer;
        const_cast<CompoDamageMat*>(this)->giveRealStressVector(answer, gp, strain, tStep);
        return answer;
    }

    int giveIPValue(FloatArray &answer, GaussPoint *gp, InternalStateType type, TimeStep *tStep) override;

    /**
     * Optional parameter determining after how many iterations within the time step the damage is calculated.
     * This is important for stress evaluation which is unequilibrated in the beginning.
     * Variables strainAtMaxStress, initDamageStress, maxStrainAtZeroStress are evaluated afterIter.
     */
    int afterIter = 0;

protected:
    /**
     * Returns 3D material stiffness matrix [6x6] in unrotated form.
     * The matrix is reduced by by omega variables.
     * @param answer Full symmetric matrix.
     * @param mode Material mode of stiffness matrix (elastic, secant).
     * @param gp Integration point.
     */
    FloatMatrixF<6,6> giveUnrotated3dMaterialStiffnessMatrix(MatResponseMode mode, GaussPoint *gp) const;
    /**
     * Returns [6x6] rotation matrix in the global coordinate system.
     * The matrix relates local c.s. to global c.s. Local c.s. can be specified with 'mcs' flag defined on element.
     * @param answer Rotation matrix [3x3].
     * @param gp Integration point.
     * @return 0 if no lcs is defined on element, 1 if defined.
     */
    int giveMatStiffRotationMatrix(FloatMatrixF<6,6> &answer, GaussPoint *gp) const;

    /// Six stress components of tension components read from the input file.
    mutable FloatArray inputTension;
    /// Six stress components of compression components read from the input file.
    mutable FloatArray inputCompression;

    /// Stress components which are allowed for snap back [6 tension, 6 compression].
    IntArray allowSnapBack;

    /**
     * Fills array elemCharLength with characteristic length related to three perpendicular planes.
     * The planes are of the same orientation as material.
     * @param status Pointer to integration point's status.
     * @param gp Integration point.
     * @param elementCs Material orientation matrix.
     */
    void giveCharLength(CompoDamageMatStatus *status, GaussPoint *gp, FloatMatrix &elementCs) const;
    /**
     * Computes characteristic length for fixed planes of material orientation.
     * @param charLenModes Returns six lengths.
     * @param gp Integration point.
     */
    void giveCharLengthForModes(FloatArray &charLenModes, GaussPoint *gp) const;
    /**
     * Check that element is small enough or Gf is large enough to prevent the snap-back.
     * @param gp Integration point.
     * @param mMode Type of material (_1dMat, _3dMat supported).
     */
    void checkSnapBack(GaussPoint *gp, MaterialMode mMode) const;
};
} // end namespace oofem
#endif // compodamagemat_h
