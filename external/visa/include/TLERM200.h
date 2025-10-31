//==============================================================================
//
// Title:		TLERM200
// Purpose:		VXIpnp driver to operate the Thorlabs ERM200.
//
// Created on:	05.07.2021 at 10:53:05
// Copyright:	Thorlabs GmbH. All Rights Reserved.
//
//==============================================================================

#ifndef __TLERM200_H__
#define __TLERM200_H__

//==============================================================================
// Include files
#include <vpptype.h>	

#ifdef __cplusplus
    extern "C" {
#endif

#if defined(_WIN64)
	#define CALLCONV            __fastcall
#elif (defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)) && !defined(_NI_mswin16_)
	#define CALLCONV            __stdcall
#endif

#ifdef IS_DLL_TARGET
	#undef _VI_FUNC
	#define _VI_FUNC __declspec(dllexport) CALLCONV
#elif defined BUILDING_DEBUG_EXE
	#undef _VI_FUNC
	#define _VI_FUNC
#else
	#undef _VI_FUNC
	#define _VI_FUNC __declspec(dllimport) CALLCONV
#endif
		
//==============================================================================
// Constants
//==============================================================================
/*========================================================================*//**
\defgroup  ERM200_Err_x Error codes
\brief     These error codes flags can be returned in a driver function
@{
*//*=========================================================================*/

// error and warning codes
#define VI_INSTR_WARNING_OFFSET        	(0x3FFC0900L)
#define VI_INSTR_ERROR_OFFSET          	(_VI_ERROR + 0x3FFC0900L)  //0xBFFC0900
		 
#define ERM200_ERR_NO_NEW_DATA      	(VI_INSTR_ERROR_OFFSET + 0x00)   ///< No new scan is available

#define ERM200_WARN_POWER_TOO_LOW		(VI_INSTR_WARNING_OFFSET + 0x01)   ///< Invalid measurement

/**@}*/   // End of defgroup ERM200_Err_x

/*========================================================================*//**
\defgroup   ERM200_x_BUFFER_x  Buffers
@{
*//*=========================================================================*/
#define ERM200_BUFFER_SIZE            256      // General buffer size
#define ERM200_ERR_DESCR_BUFFER_SIZE  512      // Buffer size for error messages
/**@}*/  /* ERM200_x_BUFFER_x */

/*========================================================================*//**
\defgroup   ERM200_POWER_UNIT_x  Unitsv
@{
*//*=========================================================================*/
#define POWER_UNIT_WATT (0)
#define POWER_UNIT_DBM  (1)
/**@}*/  /* ERM200_POWER_UNIT_x */

/*========================================================================*//**
\defgroup   ERM200_BRIGHTNESS_FLAGS_x  Status register flags
@{
*//*=========================================================================*/
#define ERM200_DISP_OFF                	(0)   ///< Display OFF
#define ERM200_DISP_ON                	(1)   ///< Display ON
#define ERM200_DISP_DIMMED              (2)   ///< Display Dimmed
/**@}*/  /* ERM200_BRIGHTNESS_FLAGS_x */ 
		
/*========================================================================*//**
\defgroup   ERM200_REGISTER_FLAGS_x  Status register flags
@{
*//*=========================================================================*/
#define ERM200_REG_STB                (0)   ///< Status Byte Register
#define ERM200_REG_SRE                (1)   ///< Service Request Enable
#define ERM200_REG_ESB                (2)   ///< Standard Event Status Register
#define ERM200_REG_ESE                (3)   ///< Standard Event Enable
#define ERM200_REG_OPER_COND          (4)   ///< Operation Condition Register
#define ERM200_REG_OPER_EVENT         (5)   ///< Operation Event Register
#define ERM200_REG_OPER_ENAB          (6)   ///< Operation Event Enable Register
#define ERM200_REG_OPER_PTR           (7)   ///< Operation Positive Transition Filter
#define ERM200_REG_OPER_NTR           (8)   ///< Operation Negative Transition Filter
#define ERM200_REG_QUES_COND          (9)   ///< Questionable Condition Register
#define ERM200_REG_QUES_EVENT         (10)  ///< Questionable Event Register
#define ERM200_REG_QUES_ENAB          (11)  ///< Questionable Event Enable Reg
#define ERM200_REG_QUES_PTR           (12)  ///< Questionable Positive Transition Filter
#define ERM200_REG_QUES_NTR           (13)  ///< Questionable Negative Transition Filter
#define ERM200_REG_MEAS_COND          (14)  ///< Measurement Condition Register
#define ERM200_REG_MEAS_EVENT         (15)  ///< Measurement Event Register
#define ERM200_REG_MEAS_ENAB          (16)  ///< Measurement Event Enable Register
#define ERM200_REG_MEAS_PTR           (17)  ///< Measurement Positive Transition Filter
#define ERM200_REG_MEAS_NTR           (18)  ///< Measurement Negative Transition Filter
#define ERM200_REG_AUX_COND           (19)  ///< Auxiliary Condition Register
#define ERM200_REG_AUX_EVENT          (20)  ///< Auxiliary Event Register
#define ERM200_REG_AUX_ENAB           (21)  ///< Auxiliary Event Enable Register
#define ERM200_REG_AUX_PTR            (22)  ///< Auxiliary Positive Transition Filter
#define ERM200_REG_AUX_NTR            (23)  ///< Auxiliary Negative Transition Filter
/**@}*/  /* ERM200_REGISTER_FLAGS_x */  

//==============================================================================
// Global functions
//==============================================================================
ViStatus _VI_FUNC TLERM200_init (ViRsrc resourceName, ViBoolean IDQuery, ViBoolean resetDevice, ViPSession instr);
ViStatus _VI_FUNC TLERM200_close (ViSession instr);

//==============================================================================
// Ressource functions
//==============================================================================
ViStatus _VI_FUNC TLERM200_findRsrc (ViSession instr, ViPUInt32 resourceCount);

ViStatus _VI_FUNC TLERM200_getRsrcName (ViSession instr, ViUInt32 index, ViChar resourceName[]);

ViStatus _VI_FUNC TLERM200_getRsrcInfo (ViSession instr,
                                        ViUInt32 index, 
										ViChar modelName[],
                                        ViChar serialNumber[],
                                        ViChar manufacturer[],
                                        ViPBoolean deviceAvailable);
//==============================================================================
// Utility functions
//==============================================================================
ViStatus _VI_FUNC TLERM200_selfTest (ViSession instr, ViPInt16 selfTestResult, ViChar selfTestMessage[]);
ViStatus _VI_FUNC TLERM200_reset (ViSession instr);
ViStatus _VI_FUNC TLERM200_revisionQuery (ViSession instr, ViChar instrumentDriverRevision[], ViChar firmwareRevision[]);
ViStatus _VI_FUNC TLERM200_errorQuery (ViSession instr, ViPInt32 errorCode, ViChar errorMessage[]);
ViStatus _VI_FUNC TLERM200_errorMessage (ViSession instr, ViStatus errorCode, ViChar errorMessage[]);
ViStatus _VI_FUNC TLERM200_getCalibrationMsg (ViSession vi, ViChar _VI_FAR str[]);
ViStatus _VI_FUNC TLERM200_identificationQuery (ViSession vi, ViChar _VI_FAR vendor[], ViChar _VI_FAR name[], ViChar _VI_FAR serial[], ViChar _VI_FAR revision[]);

ViStatus _VI_FUNC TLERM200_writeRaw (ViSession instr, ViChar command[]);
ViStatus _VI_FUNC TLERM200_readRaw (ViSession instr, ViChar buffer[], ViUInt32 bufferSize, ViPUInt32 bytesRead);

//==============================================================================
// Status Register
//==============================================================================
ViStatus _VI_FUNC TLERM200_writeRegister (ViSession instr, ViInt16 registerID, ViInt16 value);
ViStatus _VI_FUNC TLERM200_readRegister (ViSession instr, ViInt16 registerID, ViPInt16 value);
ViStatus _VI_FUNC TLERM200_presetRegister (ViSession instr);

//==============================================================================
// System functions
//==============================================================================
ViStatus _VI_FUNC TLERM200_getDisplayBrightness (ViSession instr, ViPReal64 brightness);
ViStatus _VI_FUNC TLERM200_setDisplayBrightness (ViSession instr, ViReal64 brightness);

//==============================================================================
// Configuration functions
//==============================================================================
ViStatus _VI_FUNC TLERM200_getWavelengthRange (ViSession instr, ViPUInt16 minWavelength, ViPUInt16 maxWavelength);
ViStatus _VI_FUNC TLERM200_getWavelength (ViSession instr, ViPUInt16 wavelength);
ViStatus _VI_FUNC TLERM200_setWavelength (ViSession instr,ViUInt16 wavelength);

ViStatus _VI_FUNC TLERM200_getPowerUnit (ViSession instr, ViPUInt32 unit);
ViStatus _VI_FUNC TLERM200_setPowerUnit (ViSession instr, ViUInt32 unit);

ViStatus _VI_FUNC TLERM200_getAveragingRange (ViSession instr, ViPUInt32 minAveraging, ViPUInt32 maxAveraging);
ViStatus _VI_FUNC TLERM200_getAveraging (ViSession instr, ViPUInt32 averaging);
ViStatus _VI_FUNC TLERM200_setAveraging (ViSession instr, ViUInt32 averaging);

ViStatus _VI_FUNC TLERM200_resetMinMaxMeasurement (ViSession instr);

//==============================================================================
// Measurement functions
//==============================================================================
ViStatus _VI_FUNC TLERM200_getMeasurement (ViSession instr, ViPReal64 ER, ViPReal64 phi);
ViStatus _VI_FUNC TLERM200_getMeasurementMin (ViSession instr, ViPReal64 ER, ViPReal64 phi);
ViStatus _VI_FUNC TLERM200_getMeasurementMax (ViSession instr, ViPReal64 ER, ViPReal64 phi);

ViStatus _VI_FUNC TLERM200_getPower (ViSession instr, ViPReal64 power);
ViStatus _VI_FUNC TLERM200_getEncoderVelocity (ViSession instr, ViPInt32 speed);

#ifdef __cplusplus
    }
#endif

#endif  /* ndef __TLERM200_H__ */
