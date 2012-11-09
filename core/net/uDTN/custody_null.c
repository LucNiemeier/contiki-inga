/**
* \addtogroup custody 
* @{
* \defgroup nullcust NULL custody
* @{
* \file
*
*/
#include "custody.h"

void null_cust_init(void)
{
	return;
}

uint8_t null_cust_release(struct mmem *bundle)
{
	return 0;
}

uint8_t null_cust_report(struct mmem *bundle, uint8_t status)
{
	return 0;
}

uint8_t null_cust_decide(struct mmem *bundle, uint32_t * bundle_number)
{
	return 0;
}

uint8_t null_cust_retransmit(struct mmem *bundle)
{
	return 0;
}

void null_cust_del_from_list(uint32_t bundle_num)
{
	return;
}

const struct custody_driver custody_null ={
	"NULL_CUSTODY",
	null_cust_init,
	null_cust_release,
	null_cust_report,
	null_cust_decide,
	null_cust_retransmit,
	null_cust_del_from_list
};
/** @} */
/** @} */

