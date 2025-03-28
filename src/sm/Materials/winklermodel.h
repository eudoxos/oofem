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
 *               Copyright (C) 1993 - 2014   Borek Patzak
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

#ifndef winklermodel_h
#define winklermodel_h

#include "sm/Materials/structuralmaterial.h"
#include "floatarray.h"
#include "floatmatrix.h"
#include "matconst.h"
#include "matstatus.h"
#include "interface.h"
#include "../stressstrainprincmode.h"

///@name Input fields for WinklerMaterial
//@{
#define _IFT_WinklerMaterial_Name "winkler"
#define _IFT_WinklerMaterial_C1 "c1"
#define _IFT_WinklerMaterial_globalFlag "global"
//@}

namespace oofem {

class GaussPoint;
/**
 * Implementation of 1D/2D winkler model for plate (and potentiaonnaly beam) subsoil model.
 * For plates the only C1 spring constant for deflection DOF is needed 
 * (C1 array should have size equal to 1).
 *
 * For beams the elastic constants can be an array of spring stifnesses for each individual DOF. 
 * These stifnesses may be given in global or element coordinate system.
 */
class WinklerMaterial : public StructuralMaterial
{
protected:
    /// C1 constant, defined as $\int_0^hE_{oed}(z)\left\(d\Psi(z)\over dz\right\)^2\ dz$
    FloatArray c1;
    /// Flag indicating whether subsoil model defined in global or element local c.s.
    bool globalFromulation = false;

public:
    /**
     * Constructor. Creates material with given number, belonging to given domain.
     * @param n Material number.
     * @param d Domain to which new material will belong.
     */
    WinklerMaterial(int n, Domain * d);

    bool hasMaterialModeCapability(MaterialMode mode) const override;
    const char *giveClassName() const override { return "WinklerMaterial"; }
    const char *giveInputRecordName() const override { return _IFT_WinklerMaterial_Name; }

    void initializeFrom(InputRecord &ir) override;
    void giveInputRecord(DynamicInputRecord &input) override;

    FloatArrayF<3> giveRealStressVector_2dPlateSubSoil(const FloatArrayF<3> &reducedE, GaussPoint *gp, TimeStep *tStep) const override;
    FloatArrayF<6> giveRealStressVector_3dBeamSubSoil(const FloatArrayF<6> &reducedE, GaussPoint *gp, TimeStep *tStep) const override;

    FloatMatrixF<3,3> give2dPlateSubSoilStiffMtrx(MatResponseMode mmode, GaussPoint *gp, TimeStep *tStep) const override;
    FloatMatrixF<6,6> give3dBeamSubSoilStiffMtrx(MatResponseMode mmode, GaussPoint *gp, TimeStep *tStep) const override;
    std::unique_ptr<MaterialStatus> CreateStatus(GaussPoint *gp) const override;
};

/**
 * Interface defining required functionality from associated element. 
 * The method for computing transformation matrix from global to local element c.s. is required.
 */
class OOFEM_EXPORT Beam3dSubsoilMaterialInterface : public Interface
{
public:
    /// Constructor
    Beam3dSubsoilMaterialInterface() { }
    virtual ~Beam3dSubsoilMaterialInterface() { }

    /// Evaluate transformation matrix for reciver unknowns
    virtual FloatMatrixF<6,6> B3SSMI_getUnknownsGtoLRotationMatrix() const = 0;
};

 
} // end namespace oofem
#endif // winklermodel_h
