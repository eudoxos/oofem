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

#include "compodamagemat.h"
#include "sm/Elements/structuralelement.h"
#include "gausspoint.h"
#include "sm/Materials/structuralmaterial.h"
#include "sm/Materials/structuralms.h"
#include "mathfem.h"
#include "classfactory.h"
#include "contextioerr.h"
#include "crosssection.h"

namespace oofem {
REGISTER_Material(CompoDamageMat);

CompoDamageMat :: CompoDamageMat(int n, Domain *d) : StructuralMaterial(n, d)
{
}


void CompoDamageMat :: initializeFrom(InputRecord &ir)
{
    Material :: initializeFrom(ir);

    double value;

    //define transversely othotropic material stiffness parameters
    IR_GIVE_FIELD(ir, value, _IFT_CompoDamageMat_exx);
    propertyDictionary.add(Ex, value);
    IR_GIVE_FIELD(ir, value, _IFT_CompoDamageMat_eyyezz);
    propertyDictionary.add(Ey, value);
    propertyDictionary.add(Ez, value);
    IR_GIVE_FIELD(ir, value, _IFT_CompoDamageMat_nuxynuxz);
    propertyDictionary.add(NYxy, value);
    propertyDictionary.add(NYxz, value);
    IR_GIVE_FIELD(ir, value, _IFT_CompoDamageMat_nuyz);
    propertyDictionary.add(NYyz, value);
    propertyDictionary.add(NYzy, value);
    IR_GIVE_FIELD(ir, value, _IFT_CompoDamageMat_Gxy);
    propertyDictionary.add(Gxy, value);
    propertyDictionary.add(Gxz, value);

    //calulate remaining components
    propertyDictionary.add( Gyz, this->give(Ey, NULL) / ( 1. + this->give(NYxy, NULL) ) );
    propertyDictionary.add( NYyx, this->give(Ey, NULL) * this->give(NYxy, NULL) / this->give(Ex, NULL) );
    //propertyDictionary -> add(Gzy,this->give(Gyz,NULL));
    propertyDictionary.add( NYzx, this->give(NYyx, NULL) );

    IR_GIVE_FIELD(ir, this->inputTension, _IFT_CompoDamageMat_tension_f0_gf);

    //this->inputTension.printYourself();
    IR_GIVE_FIELD(ir, this->inputCompression, _IFT_CompoDamageMat_compres_f0_gf);

    if ( this->inputTension.giveSize() != 12 ) {
        OOFEM_ERROR("need 12 components for tension in pairs f0 Gf for all 6 directions");
    }

    if ( this->inputCompression.giveSize() != 12 ) {
        OOFEM_ERROR("need 12 components for compression in pairs f0 Gf for all 6 directions");
    }

    for ( int i = 1; i <= 12; i += 2 ) {
        if ( this->inputTension.at(i) < 0.0 ) {
            OOFEM_ERROR("negative f0 detected for tension");
        }

        if ( this->inputCompression.at(i) > 0.0 ) {
            OOFEM_ERROR("positive f0 detected for compression");
        }
    }

    this->afterIter = 0;
    IR_GIVE_OPTIONAL_FIELD(ir, this->afterIter, _IFT_CompoDamageMat_afteriter);

    this->afterIter = 0;
    IR_GIVE_OPTIONAL_FIELD(ir, this->allowSnapBack, _IFT_CompoDamageMat_allowSnapBack);
}

void CompoDamageMat :: giveInputRecord(DynamicInputRecord &input)
{
    StructuralMaterial :: giveInputRecord(input);
    OOFEM_ERROR("Not implemented yet");
}


//called at the beginning of each time increment (not iteration), no influence of parameter
FloatMatrixF<6,6>
CompoDamageMat :: give3dMaterialStiffnessMatrix(MatResponseMode mode, GaussPoint *gp, TimeStep *tStep) const
{

    //already with reduced components
    auto d = this->giveUnrotated3dMaterialStiffnessMatrix(mode, gp);
    FloatMatrixF<6,6> rotationMatrix;
    if ( this->giveMatStiffRotationMatrix(rotationMatrix, gp) ) { //material rotation due to lcs
        d = rotate(d, rotationMatrix);
    }
    return d;
}

//called in each iteration, support for 3D and 1D material mode
void CompoDamageMat :: giveRealStressVector(FloatArray &answer, GaussPoint *gp, const FloatArray &totalStrain, TimeStep *tStep) const
{
    int i_max, s;
    double delta, sigma, charLen, tmp = 0., Gf_tmp;
    CompoDamageMatStatus *st = static_cast< CompoDamageMatStatus * >( this->giveStatus(gp) );
    Element *element = gp->giveElement();
    FloatArray strainVectorL(6), stressVectorL(6), tempStressVectorL(6), reducedTotalStrainVector(6), ans, equilStressVectorL(6), equilStrainVectorL(6), charLenModes(6);
    FloatArray *inputFGf;
    FloatMatrix de, elementCs(3, 3);
    MaterialMode mMode = gp->giveMaterialMode();

    st->Iteration++; //increase the call number at IP

    //subtract strain independent part - temperature, creep ..., in global c.s.
    this->giveStressDependentPartOfStrainVector(reducedTotalStrainVector, gp, totalStrain, tStep, VM_Total);
    //reducedTotalStrainVector.printYourself();

    switch ( mMode ) {
    case _3dMat: //applies only for 3D
    {
        if ( !element->giveLocalCoordinateSystem(elementCs) ) { //lcs undefined
            elementCs.resize(3, 3);
            elementCs.beUnitMatrix();
        }

        //first run
        if ( st->elemCharLength.at(1) == 0. ) {
            this->giveCharLength(st, gp, elementCs);
            //check that no snap-back occurs due to large element characteristic length (fixed-crack orientation)
            //see Bazant, Planas: Fracture and Size Effect, pp. 251
            this->checkSnapBack(gp, mMode);
        }

        //transform strain to local c.s.
        strainVectorL = this->transformStrainVectorTo(elementCs, reducedTotalStrainVector, 0);
        //strainVectorL.printYourself();

        //damage criteria based on stress, assuming same damage parameter for tension/compression
        //determine unequilibrated stress vector
        de = this->giveUnrotated3dMaterialStiffnessMatrix(SecantStiffness, gp);
        tempStressVectorL.beProductOf(de, strainVectorL);
        i_max = 6;
        break;
    }

    case _1dMat:
    { //applies only for 1D, strain vectors are already in local c.s.
        if ( st->elemCharLength.at(1) == 0. ) {
            FloatArray normal(0);
            st->elemCharLength.at(1) = gp->giveElement()->giveCharacteristicLength(normal); //truss length
            this->checkSnapBack(gp, mMode);
        }

        strainVectorL.at(1) = reducedTotalStrainVector.at(1);
        tempStressVectorL.zero();
        tempStressVectorL.at(1) = this->give(Ex, NULL) * strainVectorL.at(1);
        i_max = 1;
        break;
    }
    default:
    {
        OOFEM_ERROR("Material mode %s not supported", __MaterialModeToString(mMode) );
    }
    }

    //proceed 6 components for 3D or 1 component for 1D, damage evolution is based on the evolution of strains
    //xx, yy, zz, yz, zx, xy
    for ( int i = 1; i <= i_max; i++ ) {
        if ( tempStressVectorL.at(i) >= 0. ) { //unequilibrated stress, tension
            inputFGf = & inputTension; //contains pairs (stress - fracture energy)
            s = 0;
        } else { //unequilibrated stress, compression
            inputFGf = & inputCompression;
            s = 6;
        }

        if ( ( fabs( tempStressVectorL.at(i) ) > fabs( ( * inputFGf ).at(2 * i - 1) ) ) && ( st->strainAtMaxStress.at(i + s) == 0. ) && ( st->Iteration > this->afterIter ) ) { //damage initiated now, can be replaced for more advanced initiation criteria, e.g. Hill's maximum combination of stresses
            //equilibrated strain and stress from the last time step, transform to local c.s.
            switch ( mMode ) {
            case _3dMat:
                ans = st->giveStrainVector();
                equilStrainVectorL = this->transformStrainVectorTo(elementCs, ans, 0);
                ans = st->giveStressVector();
                equilStressVectorL = this->transformStressVectorTo(elementCs, ans, 0);
                break;

            case _1dMat:
                equilStrainVectorL = st->giveStrainVector();
                equilStressVectorL = st->giveStressVector();
                break;

            default:
                OOFEM_ERROR("Material mode %s not supported", __MaterialModeToString(mMode) );
            }

            //subdivide last increment, interpolate, delta in range <0;1>
            delta = ( ( * inputFGf ).at(2 * i - 1) - equilStressVectorL.at(i) ) / ( tempStressVectorL.at(i) - equilStressVectorL.at(i) );
            delta = min(delta, 1.);
            delta = max(delta, 0.); //stabilize transition from tensile to compression (denom -> 0)

            st->strainAtMaxStress.at(i + s) = equilStrainVectorL.at(i) + delta * ( strainVectorL.at(i) - equilStrainVectorL.at(i) );

            st->initDamageStress.at(i + s) = equilStressVectorL.at(i) + delta * ( tempStressVectorL.at(i) - equilStressVectorL.at(i) );

            //determine characteristic length for six stresses/strains
            this->giveCharLengthForModes(charLenModes, gp);
            charLen = charLenModes.at(i);

            //determine maximum strain at zero stress based on fracture energy - mode I, linear softening
            //st->maxStrainAtZeroStress.at(i + s) = st->strainAtMaxStress.at(i + s) + 2 * fabs( ( * inputFGf ).at(2 * i) ) / charLen / st->initDamageStress.at(i + s); //Gf scaled for characteristic size
            //st->maxStrainAtZeroStress.at(i + s) = 2 * fabs( ( * inputFGf ).at(2 * i) ) / charLen / st->initDamageStress.at(i + s); //Gf scaled for characteristic size
            //st->maxStrainAtZeroStress.at(i + s) = 2 * fabs( ( * inputFGf ).at(2 * i) ) / charLen / st->initDamageStress.at(i + s);

            switch ( i ) {
            case 1: tmp = this->give(Ex, NULL);
                break;
            case 2: tmp = this->give(Ey, NULL);
                break;
            case 3: tmp = this->give(Ez, NULL);
                break;
            case 4: tmp = this->give(Gyz, NULL);
                break;
            case 5: tmp = this->give(Gxz, NULL);
                break;
            case 6: tmp = this->give(Gxy, NULL);
                break;
            }

            //remaining fracture energy for the softening part in [N/m], calculated as a 1D case
            Gf_tmp = ( * inputFGf ).at(2 * i) - st->initDamageStress.at(i + s) * st->initDamageStress.at(i + s) * charLen / 2. / tmp;

            if ( Gf_tmp < 0. ) {
                OOFEM_WARNING("Too large initiation trial stress in element %d, component %d, |%f| < |%f|=f_t, negative remaining Gf=%f, treated as a snap-back", gp->giveElement()->giveNumber(), s == 0 ? i : -i, st->initDamageStress.at(i + s), tempStressVectorL.at(i), Gf_tmp);
                st->hasSnapBack.at(i) = 1;
            }

            st->maxStrainAtZeroStress.at(i + s) = st->strainAtMaxStress.at(i + s) + 2 * Gf_tmp / charLen / st->initDamageStress.at(i + s); //scaled for element's characteristic size

            //check the snap-back
            if ( fabs( st->maxStrainAtZeroStress.at(i + s) ) < fabs( st->strainAtMaxStress.at(i + s) ) && st->hasSnapBack.at(i) == 0 ) {
                OOFEM_WARNING( "Snap-back occured in element %d, component %d, |elastic strain=%f| > |fracturing strain %f|", gp->giveElement()->giveNumber(), s == 0 ? i : -i, st->strainAtMaxStress.at(i + s), st->maxStrainAtZeroStress.at(i + s) );
                st->hasSnapBack.at(i) = 1;
            }
        }

        if ( st->strainAtMaxStress.at(i + s) != 0. && fabs( strainVectorL.at(i) ) > fabs( st->kappa.at(i + s) ) ) { //damage started and grows
            //OOFEM_LOG_INFO("Damage at strain %f and stress %f (i=%d, s=%d) in GP %d element %d\n", st->strainAtMaxStress.at(i + s), tempStressVectorL.at(i), i, s,gp->giveNumber(), gp->giveElement()->giveNumber() );
            //desired stress
            sigma = st->initDamageStress.at(i + s) * ( st->maxStrainAtZeroStress.at(i + s) - strainVectorL.at(i) ) / ( st->maxStrainAtZeroStress.at(i + s) - st->strainAtMaxStress.at(i + s) );

            //check that sigma remains in tension/compression area
            if ( s == 0 ) { //tension
                sigma = max(sigma, 0.000001);
            } else {
                sigma = min(sigma, -0.000001);
            }

            switch ( i ) { //need to subtract contributions from strains
                //in equations we use nu12 = E11/E22*nu21, nu13 = E11/E33*nu31, nu23 = E22/E33*nu32
            case 1: tmp = 1. - sigma / ( this->give(Ex, NULL) * strainVectorL.at(i) + this->give(NYxy, NULL) * tempStressVectorL.at(2) + this->give(NYxz, NULL) * tempStressVectorL.at(3) );
                break;
            case 2: tmp = 1. - sigma / ( this->give(Ey, NULL) * strainVectorL.at(i) + this->give(NYyx, NULL) * tempStressVectorL.at(1) + this->give(NYyz, NULL) * tempStressVectorL.at(3) );
                break;
            case 3: tmp = 1. - sigma / ( this->give(Ez, NULL) * strainVectorL.at(i) + this->give(NYzx, NULL) * tempStressVectorL.at(1) + this->give(NYzy, NULL) * tempStressVectorL.at(2) );
                break;
            case 4: tmp = 1. - sigma / this->give(Gyz, NULL) / strainVectorL.at(i);
                break;
            case 5: tmp = 1. - sigma / this->give(Gxz, NULL) / strainVectorL.at(i);
                break;
            case 6: tmp = 1. - sigma / this->give(Gxy, NULL) / strainVectorL.at(i);
                break;
            }

            st->tempOmega.at(i) = max( tmp, st->omega.at(i) ); //damage can only grow, interval <0;1>
            st->tempOmega.at(i) = min(st->tempOmega.at(i), 0.9999);
            st->tempOmega.at(i) = max(st->tempOmega.at(i), 0.0000);
            st->tempKappa.at(i + s) = strainVectorL.at(i);

            if ( st->hasSnapBack.at(i) == 1 ) {
                st->tempOmega.at(i) = 0.9999;
            }
        }
    }

    switch ( mMode ) {
    case _3dMat: {
        //already with reduced stiffness components in local c.s.
        de = this->giveUnrotated3dMaterialStiffnessMatrix(SecantStiffness, gp);
        //de.printYourself();
        //in local c.s.
        stressVectorL.beProductOf(de, strainVectorL);
        //stressVectorL.printYourself();
        //transform local c.s to global c.s.
        st->tempStressMLCS = stressVectorL;
        answer = this->transformStressVectorTo(elementCs, stressVectorL, 1);
        break;
    }
    case _1dMat: {
        answer.resize(1);
        answer.at(1) = ( 1 - st->tempOmega.at(1) ) * this->give(Ex, NULL) * strainVectorL.at(1); //tempStress
        st->tempStressMLCS.at(1) = answer.at(1);
        break;
    }
    default:
        OOFEM_ERROR("Material mode not supported");
    }

    st->letTempStressVectorBe(answer); //needed in global c.s for 3D

    //not changed inside this function body
    st->letTempStrainVectorBe(totalStrain); //needed in global c.s for 3D
}

//used for output in *.hom a *.out
int CompoDamageMat :: giveIPValue(FloatArray &answer, GaussPoint *gp, InternalStateType type, TimeStep *tStep)
{
    CompoDamageMatStatus *status = static_cast< CompoDamageMatStatus * >( this->giveStatus(gp) );
    if ( type == IST_DamageTensor ) {
        answer = status->omega;
    } else {
        StructuralMaterial :: giveIPValue(answer, gp, type, tStep);
    }

    return 1;
}

FloatMatrixF<6,6> CompoDamageMat :: giveUnrotated3dMaterialStiffnessMatrix(MatResponseMode mode, GaussPoint *gp) const
{
    double denom;
    double ex, ey, ez, nxy, nxz, nyz, gyz, gzx, gxy;
    double a, b, c, d, e, f;
    FloatArray tempOmega;

    FloatMatrixF<6,6> answer;

    CompoDamageMatStatus *st = static_cast< CompoDamageMatStatus * >( this->giveStatus(gp) );

    ex = this->give(Ex, NULL);
    ey = this->give(Ey, NULL);
    ez = this->give(Ez, NULL);
    nxy = this->give(NYxy, NULL);
    nxz = nxy;
    nyz = this->give(NYyz, NULL);
    gyz = this->give(Gyz, NULL);
    gzx = this->give(Gxz, NULL);
    gxy = this->give(Gxy, NULL);

    //xx, yy, zz, yz, zx, xy
    //assemble stiffness matrix for transversely orthotropic material with reduced moduli, derived from compliance matrix with only reduced diagonal terms. Procedure can be used for fully orthotropic stiffness matrix as well
    a = 1. - st->tempOmega.at(1);
    b = 1. - st->tempOmega.at(2);
    c = 1. - st->tempOmega.at(3);
    d = 1. - st->tempOmega.at(4);
    e = 1. - st->tempOmega.at(5);
    f = 1. - st->tempOmega.at(6);

    if ( mode == ElasticStiffness ) {
        a = b = c = d = e = f = 0.;
    }

    denom = -ey * ex + ex * nyz * nyz * b * c * ez + nxy * nxy * a * b * ey * ey + 2 * nxy * a * b * ey * nxz * nyz * c * ez + nxz * nxz * a * ey * c * ez;

    answer.at(1, 1) = ( -ey + nyz * nyz * b * c * ez ) * a * ex * ex / denom;
    answer.at(1, 2) = -( nxy * ey + nxz * nyz * c * ez ) * ex * ey * a * b / denom;
    answer.at(1, 3) = -( nxy * nyz * b + nxz ) * ey * ex * a * c * ez / denom;
    answer.at(2, 2) = ( -ex + nxz * nxz * a * c * ez ) * b * ey * ey / denom;
    answer.at(2, 3) = -( nyz * ex + nxz * nxy * a * ey ) * ey * b * c * ez / denom;
    answer.at(3, 3) = ( -ex + nxy * nxy * a * b * ey ) * ey * c * ez / denom;
    answer.at(4, 4) = gyz;
    answer.at(5, 5) = gzx;
    answer.at(6, 6) = gxy;
    answer.symmetrized();
    //answer.printYourself();
    return answer;
}

//returns material rotation stiffness matrix [6x6]
int CompoDamageMat :: giveMatStiffRotationMatrix(FloatMatrixF<6,6> &answer, GaussPoint *gp) const
{
    FloatMatrix Lt(3, 3);
    StructuralElement *element = static_cast< StructuralElement * >( gp->giveElement() );
    MaterialMode mMode = gp->giveMaterialMode();

    switch ( mMode ) {
    case _1dMat:    //do not rotate 1D materials on trusses and beams
        break;
    case _3dMat:
        if ( !element->giveLocalCoordinateSystem(Lt) ) {    //lcs not defined on element
            return 0;
        }

        //rotate from unrotated (base) c.s. to local material c.s.
        answer = this->giveStrainVectorTranformationMtrx(Lt);
        return 1;

        break;
    default:
        OOFEM_ERROR("Material mode %s not supported", __MaterialModeToString(mMode) );
    }

    return 0;
}

// determine characteristic fracture area for three orthogonal cracks, based on the size of element (crack band model).
// Since the orientation of cracks is aligned with the orientation of material, determination is based only on the geometry (not on the direction of principal stress etc.).
// Assumption that fracture localizes into all integration points on element.
// Material orientation in global c.s. is passed. Called in the first run
void CompoDamageMat :: giveCharLength(CompoDamageMatStatus *status, GaussPoint *gp, FloatMatrix &elementCs) const
{
    FloatArray crackPlaneNormal(3);

    //elementCs.printYourself();

    //normal to x,y,z is the same as in elementCs

    for ( int i = 1; i <= 3; i++ ) {
        for ( int j = 1; j <= 3; j++ ) {
            crackPlaneNormal.at(j) = elementCs.at(j, i);
        }

        status->elemCharLength.at(i) = gp->giveElement()->giveCharacteristicLength(crackPlaneNormal);
    }
}

//determine characteristic length for six stresses/strains in their modes
void
CompoDamageMat :: giveCharLengthForModes(FloatArray &charLenModes, GaussPoint *gp) const
{
    CompoDamageMatStatus *st = static_cast< CompoDamageMatStatus * >( this->giveStatus(gp) );

    charLenModes.resize(6);
    charLenModes.at(1) = st->elemCharLength.at(1);
    charLenModes.at(2) = st->elemCharLength.at(2);
    charLenModes.at(3) = st->elemCharLength.at(3);
    charLenModes.at(4) = ( st->elemCharLength.at(2) + st->elemCharLength.at(3) ) / 2.; //average two directions
    charLenModes.at(5) = ( st->elemCharLength.at(3) + st->elemCharLength.at(1) ) / 2.; //average two directions
    charLenModes.at(6) = ( st->elemCharLength.at(1) + st->elemCharLength.at(2) ) / 2.; //average two directions
}

//check that elemnt is small enough to prevent snap-back
void CompoDamageMat :: checkSnapBack(GaussPoint *gp, MaterialMode mMode) const
{
    CompoDamageMatStatus *st = static_cast< CompoDamageMatStatus * >( this->giveStatus(gp) );
    FloatArray charLenModes(6);
    FloatArray *inputFGf;
    double l_ch, ft, Gf, elem_h, modulus = -1.0;

    for ( int j = 0; j <= 1; j++ ) {
        if ( j == 0 ) {
            inputFGf = & inputTension;
        } else {
            inputFGf = & inputCompression;
        }

        switch ( mMode ) {
        case _3dMat:
            this->giveCharLengthForModes(charLenModes, gp);
            for ( int i = 1; i <= 6; i++ ) {
                ft = fabs( ( * inputFGf ).at(2 * i - 1) );
                Gf = ( * inputFGf ).at(2 * i);
                switch ( i ) {
                case 1:
                    modulus = this->give(Ex, NULL);
                    break;
                case 2:
                    modulus = this->give(Ey, NULL);
                    break;
                case 3:
                    modulus = this->give(Ez, NULL);
                    break;
                case 4:
                    modulus = this->give(Gyz, NULL);
                    break;
                case 5:
                    modulus = this->give(Gxz, NULL);
                    break;
                case 6:
                    modulus = this->give(Gxy, NULL);
                    break;
                }

                l_ch = modulus * Gf / ft / ft;
                elem_h = charLenModes.at(i);
                if ( elem_h > 2 * l_ch ) {
                    if ( this->allowSnapBack.contains(i + 6 * j) ) {
                        OOFEM_LOG_INFO("Allowed snapback of 3D element %d GP %d Gf(%d)=%f, would need Gf>%f\n", gp->giveElement()->giveGlobalNumber(), gp->giveNumber(), j == 0 ? i : -i, Gf, ft * ft * elem_h / 2 / modulus);
                    } else {
                        OOFEM_ERROR("Decrease size of 3D element %d or increase Gf(%d)=%f to Gf>%f, possible snap-back problems", gp->giveElement()->giveNumber(), j == 0 ? i : -i, Gf, ft * ft * elem_h / 2 / modulus);
                    }
                }
            }

            break;
        case _1dMat:
            ft = fabs( ( * inputFGf ).at(1) );
            Gf = ( * inputFGf ).at(2);
            modulus = this->give(Ex, NULL);
            l_ch = modulus * Gf / ft / ft;
            elem_h = st->elemCharLength.at(1);
            if ( elem_h > 2 * l_ch ) {
                ///@todo Check value here for 1d mat (old broken code used undeclared variable)
                if ( this->allowSnapBack.contains(1 + 6 * j) ) {
                    OOFEM_LOG_INFO("Allowed snapback of 1D element %d GP %d Gf(%d)=%f, would need Gf>%f\n", gp->giveElement()->giveGlobalNumber(), gp->giveNumber(), j == 0 ? 1 : -1, Gf, ft * ft * elem_h / 2 / modulus);
                } else {
                    OOFEM_ERROR("Decrease size of 1D element %d or increase Gf(%d)=%f to Gf>%f, possible snap-back problems", gp->giveElement()->giveNumber(), j == 0 ? 1 : -1, Gf, ft * ft * elem_h / 2 / modulus);
                }
            }

            break;
        default:
            OOFEM_ERROR("Material mode %s not supported", __MaterialModeToString(mMode) );
        }
    }
}

// constructor
CompoDamageMatStatus :: CompoDamageMatStatus(GaussPoint *g) : StructuralMaterialStatus(g)
{
    //largest strain ever reached [6 tension, 6 compression]
    this->kappa.resize(12);
    this->kappa.zero();
    this->tempKappa.resize(12);
    this->tempKappa.zero();

    //array of damage parameters [6] for both tension and compression
    this->omega.resize(6);
    this->omega.zero();
    this->tempOmega.resize(6);
    this->tempOmega.zero();
    this->hasSnapBack.resize(6);
    this->hasSnapBack.zero();

    this->initDamageStress.resize(12);
    this->initDamageStress.zero();
    this->maxStrainAtZeroStress.resize(12);
    this->maxStrainAtZeroStress.zero();
    this->strainAtMaxStress.resize(12);
    this->strainAtMaxStress.zero();

    this->tempStressMLCS.resize(6);
    this->tempStressMLCS.zero();

    this->elemCharLength.resize(3);
    this->elemCharLength.zero();
}


void CompoDamageMatStatus :: printOutputAt(FILE *file, TimeStep *tStep) const
{
    int maxComponents = 0;
    StructuralMaterialStatus :: printOutputAt(file, tStep);
    fprintf(file, "status {");
    switch ( gp->giveMaterialMode() ) {
    case _3dMat: {
        maxComponents = 6;
        break;
    }
    case _1dMat: {
        maxComponents = 1;
        break;
    }
    default:
        OOFEM_ERROR("Material mode not supported");
    }

    if ( !this->omega.containsOnlyZeroes() ) {
        fprintf(file, " omega ");
        for ( int i = 1; i <= maxComponents; i++ ) {
            fprintf( file, "%.4f ", this->omega.at(i) );
        }
    }

    fprintf(file, " Local_stress ");
    for ( int i = 1; i <= maxComponents; i++ ) {
        fprintf( file, "%.2e ", this->tempStressMLCS.at(i) );
    }

    fprintf(file, " kappa "); //print pairs tension-compression
    for ( int i = 1; i <= maxComponents; i++ ) {
        for ( int j = 0; j < 2; j++ ) {
            fprintf( file, "%.2e ", this->kappa.at(6 * j + i) );
        }
    }

    fprintf(file, "}\n");
}


//initializes temp variables according to variables form previous equilibrium state, resets tempStressVector, tempStrainVector
//function called at the beginning of each time increment (not iteration)
void CompoDamageMatStatus :: initTempStatus()
{ }

// updates variables (nonTemp variables describing situation at previous equilibrium state)
// after a new equilibrium state has been reached
// temporary variables are having values corresponding to newly reached equilibrium
void CompoDamageMatStatus :: updateYourself(TimeStep *tStep)
{
    //here stressVector = tempStressVector; strainVector = tempStrainVector;
    StructuralMaterialStatus :: updateYourself(tStep); //MaterialStatus::updateYourself, i.e. stressVector = tempStressVector; strainVector = tempStrainVector;
    this->kappa = this->tempKappa;
    this->omega = this->tempOmega;
    this->Iteration = 0;
}


void CompoDamageMatStatus :: saveContext(DataStream &stream, ContextMode mode)
{
    StructuralMaterialStatus :: saveContext(stream, mode);
}

void CompoDamageMatStatus :: restoreContext(DataStream &stream, ContextMode mode)
{
    StructuralMaterialStatus :: restoreContext(stream, mode);
}
} // end namespace oofem
