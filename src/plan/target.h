#ifndef __RTS_TARGET__
#define __RTS_TARGET__
#include <errno.h>
#include <libnova/libnova.h>
#include <pthread.h>
#include <stdio.h>
#include <time.h>

#include "image_info.h"
#include "status.h"

#include "../utils/objectcheck.h"

#define MAX_READOUT_TIME		120
#define EXPOSURE_TIMEOUT		50

#define EXPOSURE_TIME		60

#define TYPE_OPORTUNITY         'O'
#define TYPE_GRB                'G'
#define TYPE_GRB_TEST		'g'
#define TYPE_SKY_SURVEY         'S'
#define TYPE_GPS                'P'
#define TYPE_ELLIPTICAL         'E'
#define TYPE_HETE               'H'
#define TYPE_PHOTOMETRIC	'M'
#define TYPE_TECHNICAL          't'
#define TYPE_TERESTIAL		'T'
#define TYPE_CALIBRATION	'c'

#define COMMAND_EXPOSURE	'E'
#define COMMAND_FILTER		'F'
#define COMMAND_PHOTOMETER      'P'
#define COMMAND_CHANGE		'C'
#define COMMAND_WAIT            'W'
#define COMMAND_SLEEP		'S'
#define COMMAND_UTC_SLEEP	'U'
#define COMMAND_CENTERING	'c'
#define	COMMAND_FOCUSING	'f'

#define TARGET_FOCUSING		11

struct thread_list
{
  pthread_t thread;
  struct thread_list *next;
};

class Target;

struct ex_info
{
  struct device *camera;
  Target *last;
  int move_count;
};

/**
 * Class for one observation.
 *
 * It is created when it's possible to observer such target, and when such
 * observation have some scientific value.
 * 
 * It's put on the observation que inside planc. It's then executed.
 *
 * During execution it can do centering (acquiring images to better know scope position),
 * it can compute importance of future observations.
 *
 * After execution planc select new target for execution, run image processing
 * on first observation target in the que.
 *
 * After all images are processed, new priority can be computed, some results
 * (e.g. light curves) can be put to the database.
 *
 * Target is mainly backed by observation entry in the DB. It uses obs_state
 * field to store states of observation.
 */
class Target
{
private:
  struct thread_list thread_l;

  int get_info (struct device *cam,
		float exposure, hi_precision_t * img_hi_precision);

  int dec_script_thread_count (void);
  int join_script_thread ();

  pthread_mutex_t move_count_mutex;
  pthread_cond_t move_count_cond;
  int move_count;

  int precission_count;

  pthread_mutex_t script_thread_count_mutex;
  pthread_cond_t script_thread_count_cond;

  int script_thread_count;

  pthread_mutex_t closed_loop_mutex;
  pthread_cond_t closed_loop_cond;

  hi_precision_t closed_loop_precission;

  int running_script_count;	// number of running scripts (not in W)

  int obs_id;

  int tel_target_state;
protected:
  int target_id;

  struct device *telescope;
  struct ln_lnlat_posn *observer;

  virtual int runScript (struct ex_info *exinfo);	// script for each device
  virtual int get_script (struct device *camera, char *buf);

  // print nice formated log strings
  void logMsg (const char *message);
  void logMsg (const char *message, int num);
  void logMsg (const char *message, long num);
  void logMsg (const char *message, double num);
  void logMsg (const char *message, const char *val);
public:
    Target (int in_tar_id, struct ln_lnlat_posn *in_obs);
  int getPosition (struct ln_equ_posn *pos)
  {
    return getPosition (pos, ln_get_julian_from_sys ());
  };
  virtual int getPosition (struct ln_equ_posn *pos, double JD)
  {
    return -1;
  };
  void setHiPrecission (int in_hi_precission)
  {
    hi_precision = in_hi_precission;
  };
  virtual int getRST (struct ln_rst_time *rst, double jd)
  {
    return -1;
  };
  int wait_for_readout_end ()
  {
    struct timespec timeout;
    time_t now;
    int ret;

    pthread_mutex_lock (&script_thread_count_mutex);
    while (running_script_count > 0)	// cause we will hold running_script_count lock in any case..
      {
	pthread_cond_wait (&script_thread_count_cond,
			   &script_thread_count_mutex);
      }
    pthread_mutex_unlock (&script_thread_count_mutex);
    if (hi_precision == 3 && type == TARGET_LIGHT)
      {
	pthread_mutex_lock (closed_loop_precission.mutex);
	time (&now);
	timeout.tv_sec = now + 350;
	timeout.tv_nsec = 0;
	ret = 0;
	while (closed_loop_precission.processed == 0 && ret != ETIMEDOUT)
	  {
	    ret = pthread_cond_timedwait (closed_loop_precission.cond,
					  closed_loop_precission.mutex,
					  &timeout);
	  }
	printf
	  ("closed_loop_precission->image_pos->ra: %f dec: %f closed_loop_precission.hi_precision %i\n",
	   closed_loop_precission.image_pos.ra,
	   closed_loop_precission.image_pos.dec,
	   closed_loop_precission.hi_precision);
	pthread_mutex_unlock (closed_loop_precission.mutex);
      }
  };
  int move ();			// change position
  static void *runStart (void *exinfo);	// entry point for camera threads
  virtual int observe (Target * last);
  virtual int acquire ();	// one extra thread
  virtual int run ();		// start per device threads
  virtual int postprocess ();	// run in extra thread
  virtual int considerForObserving (ObjectCheck * checker, double lst);	// return 0, when target can be observed, otherwise modify tar_bonus..
  virtual int dropBonus ();
  virtual int changePriority (int pri_change, double validJD);
  int type;			// light, dark, flat, flat_dark
  char obs_type;		// SKY_SURVEY, GBR, .. 
  time_t start_time;
  int moved;
  int tolerance;
  int hi_precision;		// when 1, image will get imediate astrometry and telescope status will be updated
};

class ConstTarget:public Target
{
private:
  struct ln_equ_posn position;
public:
    ConstTarget (int in_tar_id, struct ln_lnlat_posn *in_obs);
  virtual int getPosition (struct ln_equ_posn *pos, double JD);
  virtual int getRST (struct ln_rst_time *rst, double jd);
};

class TargetFocusing:public ConstTarget
{
protected:
  virtual int get_script (struct device *camera, char *buf);
public:
    TargetFocusing (int in_tar_id,
		    struct ln_lnlat_posn *in_obs):ConstTarget (in_tar_id,
							       in_obs)
  {
  };
};

class EllTarget:public Target
{
private:
  struct ln_ell_orbit orbit;
public:
    EllTarget (int in_tar_id, struct ln_lnlat_posn *in_obs);
  virtual int getPosition (struct ln_equ_posn *pos, double JD);
  virtual int getRST (struct ln_rst_time *rst, double jd);
};

class LunarTarget:public Target
{
public:
  LunarTarget (int in_tar_id, struct ln_lnlat_posn *in_obs);
  virtual int getPosition (struct ln_equ_posn *pos, double JD);
  virtual int getRST (struct ln_rst_time *rst, double jd);
};

class TargetGRB:public ConstTarget
{
protected:
//  virtual int get_script (struct device *camera, char *buf);
public:
  TargetGRB (int in_tar_id,
	     struct ln_lnlat_posn *in_obs):ConstTarget (in_tar_id, in_obs)
  {
  };
};

#endif /*! __RTS_TARGET__ */
