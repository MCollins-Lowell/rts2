/*!
 * @file Useful to everyone willing to write informations in fits format.
 *
 * @author petr
 */

#ifndef __RTS_FITS__
#define __RTS_FITS__

#include <fitsio.h>
#include <pthread.h>

#include "telescope_info.h"
#include "camera_info.h"

/*! 
 * Structure to hold additional fits informations.
 */
struct fits_receiver_data
{
  int offset;
  size_t size;
  fitsfile *ffile;
  char *data;
  int header_processed;
  pthread_t thread;
};

int fits_create (struct fits_receiver_data *receiver, char *filename);

int fits_write_camera (struct fits_receiver_data *receiver,
		       struct camera_info *camera, float exposure,
		       time_t * exp_start);
int fits_write_telescope (struct fits_receiver_data *receiver,
			  struct telescope_info *telescope);

int fits_init (struct fits_receiver_data *receiver, size_t expected_size);

int fits_handler (void *data, size_t size,
		  struct fits_receiver_data *receiver);

int fits_close (struct fits_receiver_data *receiver);

#endif /* !__RTS_FITS__ */
