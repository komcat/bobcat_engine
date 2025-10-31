//==============================================================================
//
// Title:		TLSPCNT
// Purpose:		A short description of the interface.
//

// Copyright:	Thorlabs GmbH. All Rights Reserved.
//
//==============================================================================

#ifndef __TLSPCNT_H__
#define __TLSPCNT_H__

//==============================================================================
// Include files

#include <vpptype.h>

//==============================================================================
// Types

#if defined(__cplusplus) || defined(__cplusplus__)
    extern "C" {
#endif
		
		
#if defined(_WIN64)
	#define CALLCONV            __fastcall
#elif (defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)) && !defined(_NI_mswin16_)
	#define CALLCONV            __stdcall
#endif

#ifndef _VI_FUNC
	#define _VI_FUNC __declspec(dllexport) CALLCONV
#endif	
  
/*========================================================================*//**
\defgroup  SPCNT_Err_x Error codes
\brief     These error codes flags can be returned in a driver function
@{
*//*=========================================================================*/

// error and warning codes
#define VI_INSTR_WARNING_OFFSET                    		(0x3FFC0900L)
#define VI_INSTR_ERROR_OFFSET          		(_VI_ERROR + 0x3FFC0900L)  //0xBFFC0900
		 
#define SPCNT_ERR_NO_NEW_DATA      			(VI_INSTR_ERROR_OFFSET + 0x00)   ///< No new scan is available

#define SPCNT_WARN_NO_BEAM_WIDTH_CLIPX		(VI_INSTR_WARNING_OFFSET + 0x01)   ///< Invalid beam width x

/**@}*/   // End of defgroup SPCNT_Err_x

/*========================================================================*//**
\defgroup   SPCNT_x_BUFFER_x  Buffers
@{
*//*=========================================================================*/
#define SPCNT_BUFFER_SIZE            256      // General buffer size
#define SPCNT_ERR_DESCR_BUFFER_SIZE  512      // Buffer size for error messages
#define SPCNT_MAX_ARRAY_DATA 		(1000)	  // max entries in the array buffer		
/**@}*/  /* SPCNT_x_BUFFER_x */

/*========================================================================*//**
\defgroup   SPCNT_REGISTER_FLAGS_x  Status register flags
@{
*//*=========================================================================*/
#define SPCNT_REG_STB                (0)   ///< Status Byte Register
#define SPCNT_REG_SRE                (1)   ///< Service Request Enable
#define SPCNT_REG_ESB                (2)   ///< Standard Event Status Register
#define SPCNT_REG_ESE                (3)   ///< Standard Event Enable
#define SPCNT_REG_OPER_COND          (4)   ///< Operation Condition Register
#define SPCNT_REG_OPER_EVENT         (5)   ///< Operation Event Register
#define SPCNT_REG_OPER_ENAB          (6)   ///< Operation Event Enable Register
#define SPCNT_REG_OPER_PTR           (7)   ///< Operation Positive Transition Filter
#define SPCNT_REG_OPER_NTR           (8)   ///< Operation Negative Transition Filter
#define SPCNT_REG_QUES_COND          (9)   ///< Questionable Condition Register
#define SPCNT_REG_QUES_EVENT         (10)  ///< Questionable Event Register
#define SPCNT_REG_QUES_ENAB          (11)  ///< Questionable Event Enable Reg
#define SPCNT_REG_QUES_PTR           (12)  ///< Questionable Positive Transition Filter
#define SPCNT_REG_QUES_NTR           (13)  ///< Questionable Negative Transition Filter
/**@}*/  /* SPCNT_REGISTER_FLAGS_x */  

/*========================================================================*//**
\defgroup   SPCNT_BRIGHTNESS_FLAGS_x  Status register flags
@{
*//*=========================================================================*/
#define SPCNT_DISP_OFF                	(0)   ///< Display OFF
#define SPCNT_DISP_ON                	(1)   ///< Display ON
#define SPCNT_DISP_DIMMED               (2)   ///< Display Dimmed
/**@}*/  /* SPCNT_BRIGHTNESS_FLAGS_x */ 

//==============================================================================
// Global functions
		
ViStatus _VI_FUNC TLSPCNT_init (ViRsrc resourceName, ViBoolean IDQuery, ViBoolean resetDevice, ViPSession instr);

ViStatus _VI_FUNC TLSPCNT_findRsrc (ViSession instr, ViUInt32 *resourceCount);
ViStatus _VI_FUNC TLSPCNT_getRsrcName (ViSession instr, ViUInt32 index, ViChar resourceName[]);
ViStatus _VI_FUNC TLSPCNT_getRsrcInfo (ViSession instr,
                                       ViUInt32 index, ViChar modelName[],
                                       ViChar serialNumber[],
                                       ViChar manufacturer[],
                                       ViBoolean *resourceAvailable);

ViStatus _VI_FUNC TLSPCNT_error_message (ViSession instr, ViStatus errorCode, ViChar errorMessage[]);
ViStatus _VI_FUNC TLSPCNT_error_query (ViSession instr, ViPInt32 errorCode, ViChar errorMessage[]);
ViStatus _VI_FUNC TLSPCNT_reset (ViSession instr);
ViStatus _VI_FUNC TLSPCNT_self_test (ViSession instr, ViPInt16 selfTestResult, ViChar selfTestMessage[]);
ViStatus _VI_FUNC TLSPCNT_revision_query (ViSession instr, ViChar instrumentDriverRevision[], ViChar firmwareRevision[]);
ViStatus _VI_FUNC TLSPCNT_identification_query (ViSession instr,ViChar manunfacturer[],ViChar deviceName[],ViChar serialNumber[]);

ViStatus _VI_FUNC TLSPCNT_getCalibrationMessage (ViSession instr, ViChar calibrationMessage[]);

ViStatus _VI_FUNC TLSPCNT_writeRegister (ViSession instr, ViInt16 registerID, ViUInt16 value);
ViStatus _VI_FUNC TLSPCNT_readRegister (ViSession instr, ViInt16 registerID, ViPUInt16 value);
ViStatus _VI_FUNC TLSPCNT_presetRegister (ViSession instr);

ViStatus _VI_FUNC TLSPCNT_setDisplayBrightness (ViSession instr, ViUInt16 brightness);
ViStatus _VI_FUNC TLSPCNT_getDisplayBrightness (ViSession instr, ViPUInt16 brightness);

ViStatus _VI_FUNC TLSPCNT_startZeroing (ViSession instr);
ViStatus _VI_FUNC TLSPCNT_abortZeroing (ViSession instr);
ViStatus _VI_FUNC TLSPCNT_getZeroState (ViSession instr, ViPBoolean state);
ViStatus _VI_FUNC TLSPCNT_setZeroValue (ViSession instr, ViReal64 zeroValue);
ViStatus _VI_FUNC TLSPCNT_getZeroValue (ViSession instr, ViPReal64 zeroValue);

ViStatus _VI_FUNC TLSPCNT_setFrequencyCountThreshold (ViSession instr, ViUInt16 threshold);
ViStatus _VI_FUNC TLSPCNT_getFrequencyCountThreshold (ViSession instr, ViPUInt16 threshold);

ViStatus _VI_FUNC TLSPCNT_startFrequencyCounting (ViSession instr);
ViStatus _VI_FUNC TLSPCNT_stopFrequencyCounting (ViSession instr);
ViStatus _VI_FUNC TLSPCNT_getFrequencyCountingState (ViSession instr, ViPBoolean active);

ViStatus _VI_FUNC TLSPCNT_setArrayLength (ViSession instr, ViUInt32 length);
ViStatus _VI_FUNC TLSPCNT_getArrayLength (ViSession instr, ViPUInt32 length);

ViStatus _VI_FUNC TLSPCNT_setBinWidth (ViSession instr, ViUInt32 width);
ViStatus _VI_FUNC TLSPCNT_getBinWidth (ViSession instr, ViPUInt32 width);

ViStatus _VI_FUNC TLSPCNT_setDeadtime (ViSession instr, ViUInt32 time);
ViStatus _VI_FUNC TLSPCNT_getDeadtime (ViSession instr, ViPUInt32 time);

ViStatus _VI_FUNC TLSPCNT_setAverageCount (ViSession instr, ViUInt16 averageCount);
ViStatus _VI_FUNC TLSPCNT_getAverageCount (ViSession instr, ViPUInt16 averageCount);

ViStatus _VI_FUNC TLSPCNT_resetStatistics (ViSession instr);

ViStatus _VI_FUNC TLSPCNT_getFrequency (ViSession instr, 
										ViPReal64 frequency, 
										ViPReal64 minValue,                             
										ViPReal64 maxValue,
                                        ViPReal64 averageValue);
ViStatus _VI_FUNC TLSPCNT_getCount (ViSession instr, ViPInt32 count);
ViStatus _VI_FUNC TLSPCNT_getTime (ViSession instr, ViPReal64 time);
ViStatus _VI_FUNC TLSPCNT_getBins (ViSession instr, ViInt32 bins[], ViPUInt32 len);

ViStatus _VI_FUNC TLSPCNT_close (ViSession instr);

#if defined(__cplusplus) || defined(__cplusplus__)
    }
#endif

#endif  /* ndef __TLSPCNT_H__ */
