/*
 * MSPScopedPerfCheck.h
 *
 *	@brief - MSP Performance check utilities
 *
 *  Created on: Feb 20, 2013
 *      Author: pradeep
 */

#ifndef MSPSCOPEDPERFCHECK_H_
#define MSPSCOPEDPERFCHECK_H_

/*
 *
 */

/** @brief - Defines to calculate the time  taken by a function */
#define __PERF_DEBUG__

#ifdef __PERF_DEBUG__
#include<sys/time.h>

#define START_TIME struct timeval start_clock; gettimeofday(&start_clock,NULL);
#define END_TIME   struct timeval end_clock;   gettimeofday(&end_clock,NULL);
#define CALCULATE_EXEC_TIME(st,end)	{				\
                res.tv_sec  = end.tv_sec - st.tv_sec ;			\
                res.tv_usec = end.tv_usec - st.tv_usec;			\
                while(res.tv_usec<0) {					\
                                res.tv_usec += 1000000;			\
                                res.tv_sec -= 1;			\
                }							\
                res.tv_usec = res.tv_usec/1000;				\
	}

#define PRINT_EXEC_TIME {						\
                struct timeval res;					\
		CALCULATE_EXEC_TIME(start_clock,end_clock);		\
		dlog( DL_MSP_MPLAYER, DLOGL_NOISE,"\n#MSP_PROFILE:%s():exec-time: %ld sec %ld msec \n",__FUNCTION__,res.tv_sec,res.tv_usec); \
	}

#ifdef __cplusplus
class MSPScopedPerfCheck
{
public:
    MSPScopedPerfCheck(const char* function, int line, long logMsThreshold = 0, const char* xtraLogMsg = NULL)
        : m_function(function), m_line(line), m_logMsThreshold(logMsThreshold), m_xtraLogMsg(xtraLogMsg)
    {
        gettimeofday(&m_start, NULL);
    }

    ~MSPScopedPerfCheck()
    {
        struct timeval end;
        gettimeofday(&end, NULL);

        long execMs = ((end.tv_sec - m_start.tv_sec) * 1000) + (end.tv_usec - m_start.tv_usec) / 1000;
        if (execMs >= m_logMsThreshold)
            dlog(DL_MSP_MPLAYER, DLOGL_NOISE, "%s:%d scoped-exec-time:%04ld ms.  %s", m_function, m_line, execMs, m_xtraLogMsg ? m_xtraLogMsg : "");
    }
    const char* m_function;
    int m_line;
    struct timeval m_start;
    long m_logMsThreshold;
    const char *m_xtraLogMsg;
};
#define MSP_SCOPED_PERF_CHECK  MSPScopedPerfCheck spc## __LINE__ (__FUNCTION__,__LINE__)// The ## __LINE__ part allows timing to be started at several different points
#define MSP_SCOPED_PERF_CHECK_THRESHOLD(x,xtraText)  MSPScopedPerfCheck spc## __LINE__ (__FUNCTION__,__LINE__,(x),(xtraText))

#endif //__cplusplus


#else // ! defined __PERF_DEBUG__

#define START_TIME
#define END_TIME
#define PRINT_EXEC_TIME
#define MSP_SCOPED_PERF_CHECK
#define MSP_SCOPED_PERF_CHECK_THRESHOLD(x,xtraText)

#endif // __PERF_DEBUG__

#define START TIME

/*
 *  @brief HOST debug time string function declaration
 */

#ifdef HOST
#ifdef __cplusplus\
extern "C" {
#endif
/*
 * \return returns char string for current time.
 * \brief - Helper function to get time string for current time, used to print current time on host.
 */
extern char *getTimeString(void);
/*
 *  @brief HOST debug dlog() function re-declaration.
 */
#undef dlog
#include <stdio.h>
#define dlog(x,y,z...) do { fprintf(stdout, "%s : ", getTimeString()); fprintf (stdout, z); fprintf (stdout, "\n"); } while (0)

#ifdef __cplusplus
}
#endif // __cplusplus
#endif //HOST


#endif /* MSPSCOPEDPERFCHECK_H_ */
