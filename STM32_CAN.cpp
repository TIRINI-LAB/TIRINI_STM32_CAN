#include "STM32_CAN.h"

//Max value, lowest priority
#define MAX_IRQ_PRIO_VALUE ((1UL << __NVIC_PRIO_BITS) - 1UL)

constexpr Baudrate_entry_t STM32_CAN::BAUD_RATE_TABLE_48M[];
constexpr Baudrate_entry_t STM32_CAN::BAUD_RATE_TABLE_45M[];

static STM32_CAN* _CAN1 = nullptr;
static CAN_HandleTypeDef     hcan1;
uint32_t test = 0;
#ifdef CAN2
static STM32_CAN* _CAN2 = nullptr;
static CAN_HandleTypeDef     hcan2;
#endif
#ifdef CAN3
static STM32_CAN* _CAN3 = nullptr;
static CAN_HandleTypeDef     hcan3;
#endif

STM32_CAN::STM32_CAN(PinName rx, PinName tx, RXQUEUE_TABLE rxSize, TXQUEUE_TABLE txSize)
  : rx(rx), tx(tx), sizeRxBuffer(rxSize), sizeTxBuffer(txSize),
    mode(Mode::NORMAL), preemptPriority(MAX_IRQ_PRIO_VALUE), subPriority(0)
{
  init();
}

STM32_CAN::STM32_CAN( CAN_TypeDef* canPort, RXQUEUE_TABLE rxSize, TXQUEUE_TABLE txSize )
  : sizeRxBuffer(rxSize), sizeTxBuffer(txSize),
    mode(Mode::NORMAL), preemptPriority(MAX_IRQ_PRIO_VALUE), subPriority(0)
{
  //get first matching pins from map
  rx = pinmap_find_pin(canPort, PinMap_CAN_RD);
  tx = pinmap_find_pin(canPort, PinMap_CAN_TD);
  init();
}

//lagacy pin config for compatibility
STM32_CAN::STM32_CAN( CAN_TypeDef* canPort, CAN_PINS pins, RXQUEUE_TABLE rxSize, TXQUEUE_TABLE txSize )
  : rx(NC), tx(NC), sizeRxBuffer(rxSize), sizeTxBuffer(txSize),
    mode(Mode::NORMAL), preemptPriority(MAX_IRQ_PRIO_VALUE), subPriority(0)
{
  if (canPort == CAN1)
  {
    switch(pins)
    {
      case DEF:
        rx = PA_11;
        tx = PA_12;
        break;
      case ALT:
        rx = PB_8;
        tx = PB_9;
        break;
      #if defined(__HAL_RCC_GPIOD_CLK_ENABLE)
      case ALT_2:
        rx = PD_0;
        tx = PD_1;
        break;
      #endif
    }
  }
#ifdef CAN2
  else if(canPort == CAN2)
  {
    switch(pins)
    {
      case DEF:
        rx = PB_12;
        tx = PB_13;
        break;
      case ALT:
        rx = PB_5;
        tx = PB_6;
        break;
    }
  }
#endif
#ifdef CAN3
  else if(canPort == CAN3)
  {
    switch(pins)
    {
      case DEF:
        rx = PA_8;
        tx = PA_15;
        break;
      case ALT:
        rx = PB_3;
        tx = PB_4;
        break;
    }
  }
#endif
  init();
}

void STM32_CAN::init(void)
{
  CAN_TypeDef * canPort_rx = (CAN_TypeDef *) pinmap_peripheral(rx, PinMap_CAN_RD);
  CAN_TypeDef * canPort_tx = (CAN_TypeDef *) pinmap_peripheral(tx, PinMap_CAN_TD);
  if ((canPort_rx != canPort_tx && canPort_tx != NP) || canPort_rx == NP)
  {
    //ensure pins relate to same peripheral OR only Rx is set/valid
    // rx only can be used as listen only but needs a 3rd node for valid ACKs

    // do not allow Tx only since that would break arbitration
    return;
  }

  //clear tx pin in case it was set but does not match a peripheral
  if(canPort_tx == NP)
    tx = NC;

  if (canPort_rx == CAN1)
  {
    _CAN1 = this;
    n_pCanHandle = &hcan1;
  }
  #ifdef CAN2
  if( canPort_rx == CAN2)
  {
    _CAN2 = this;
    n_pCanHandle = &hcan2;
  }
  #endif
  #ifdef CAN3
  if (canPort_rx == CAN3)
  {
    _CAN3 = this;
    n_pCanHandle = &hcan3;
  }
  #endif

  _canPort = canPort_rx;
}

void STM32_CAN::setIRQPriority(uint32_t preemptPriority, uint32_t subPriority)
{
  //NOTE: limiting the IRQ prio, but not accounting for group setting
  this->preemptPriority = min(preemptPriority, MAX_IRQ_PRIO_VALUE);
  this->subPriority = min(subPriority, MAX_IRQ_PRIO_VALUE);
}

// Init and start CAN
void STM32_CAN::begin( bool retransmission ) {

  // exit if CAN already is active
  if (_canIsActive) return;

  _canIsActive = true;

  initializeBuffers();
  
  pin_function(rx, pinmap_function(rx, PinMap_CAN_RD));
  pin_function(tx, pinmap_function(tx, PinMap_CAN_TD));

  // Configure CAN
  if (_canPort == CAN1)
  {
    //CAN1
    __HAL_RCC_CAN1_CLK_ENABLE();

    #ifdef CAN1_IRQn_AIO
    // NVIC configuration for CAN1 common interrupt
    HAL_NVIC_SetPriority(CAN1_IRQn_AIO, preemptPriority, subPriority);
    HAL_NVIC_EnableIRQ(CAN1_IRQn_AIO);
    #else
    // NVIC configuration for CAN1 Reception complete interrupt
    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, preemptPriority, subPriority);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn );
    // NVIC configuration for CAN1 Transmission complete interrupt
    HAL_NVIC_SetPriority(CAN1_TX_IRQn,  preemptPriority, subPriority);
    HAL_NVIC_EnableIRQ(CAN1_TX_IRQn);
    #endif /** else defined(CAN1_IRQn_AIO) */

    n_pCanHandle->Instance = CAN1;
  }
#ifdef CAN2
  else if (_canPort == CAN2)
  {
    //CAN2
    __HAL_RCC_CAN1_CLK_ENABLE(); // CAN1 clock needs to be enabled too, because CAN2 works as CAN1 slave.
    __HAL_RCC_CAN2_CLK_ENABLE();

    // NVIC configuration for CAN2 Reception complete interrupt
    HAL_NVIC_SetPriority(CAN2_RX0_IRQn, preemptPriority, subPriority);
    HAL_NVIC_EnableIRQ(CAN2_RX0_IRQn );
    // NVIC configuration for CAN2 Transmission complete interrupt
    HAL_NVIC_SetPriority(CAN2_TX_IRQn,  preemptPriority, subPriority);
    HAL_NVIC_EnableIRQ(CAN2_TX_IRQn);

    n_pCanHandle->Instance = CAN2;
  }
#endif

#ifdef CAN3
  else if (_canPort == CAN3)
  {
    //CAN3
    __HAL_RCC_CAN3_CLK_ENABLE();

    // NVIC configuration for CAN3 Reception complete interrupt
    HAL_NVIC_SetPriority(CAN3_RX0_IRQn, preemptPriority, subPriority);
    HAL_NVIC_EnableIRQ(CAN3_RX0_IRQn );
    // NVIC configuration for CAN3 Transmission complete interrupt
    HAL_NVIC_SetPriority(CAN3_TX_IRQn,  preemptPriority, subPriority);
    HAL_NVIC_EnableIRQ(CAN3_TX_IRQn);

    n_pCanHandle->Instance = CAN3;
  }
#endif

  n_pCanHandle->Init.TimeTriggeredMode = DISABLE;
  n_pCanHandle->Init.AutoBusOff = DISABLE;
  n_pCanHandle->Init.AutoWakeUp = DISABLE;
  if (retransmission){ n_pCanHandle->Init.AutoRetransmission  = ENABLE; }
  else { n_pCanHandle->Init.AutoRetransmission  = DISABLE; }
  n_pCanHandle->Init.ReceiveFifoLocked  = DISABLE;
  n_pCanHandle->Init.TransmitFifoPriority = ENABLE;
  n_pCanHandle->Init.Mode = mode;
}

void STM32_CAN::setBaudRate(uint32_t baud)
{

  // Calculate and set baudrate
  calculateBaudrate( n_pCanHandle, baud );

  // Initializes CAN
  HAL_CAN_Init( n_pCanHandle );

  initializeFilters();

  // Start the CAN peripheral
  HAL_CAN_Start( n_pCanHandle );

  // Activate CAN RX notification
  HAL_CAN_ActivateNotification( n_pCanHandle, CAN_IT_RX_FIFO0_MSG_PENDING);

  // Activate CAN TX notification
  HAL_CAN_ActivateNotification( n_pCanHandle, CAN_IT_TX_MAILBOX_EMPTY);
}

bool STM32_CAN::write(CAN_message_t &CAN_tx_msg, bool sendMB)
{
  bool ret = true;
  uint32_t TxMailbox;
  CAN_TxHeaderTypeDef TxHeader;

  __HAL_CAN_DISABLE_IT(n_pCanHandle, CAN_IT_TX_MAILBOX_EMPTY);

  if (CAN_tx_msg.flags.extended == 1) // Extended ID when CAN_tx_msg.flags.extended is 1
  {
      TxHeader.ExtId = CAN_tx_msg.id;
      TxHeader.IDE   = CAN_ID_EXT;
  }
  else // Standard ID otherwise
  {
      TxHeader.StdId = CAN_tx_msg.id;
      TxHeader.IDE   = CAN_ID_STD;
  }

  if (CAN_tx_msg.flags.remote == 1) // Remote frame when CAN_tx_msg.flags.remote is 1
  {
    TxHeader.RTR   = CAN_RTR_REMOTE;
    TxHeader.DLC   = 0;
  }
  else{
    TxHeader.RTR   = CAN_RTR_DATA;
    TxHeader.DLC   = CAN_tx_msg.len;
  }

  TxHeader.TransmitGlobalTime = DISABLE;

  if(HAL_CAN_AddTxMessage( n_pCanHandle, &TxHeader, CAN_tx_msg.buf, &TxMailbox) != HAL_OK)
  {
    /* in normal situation we add up the message to TX ring buffer, if there is no free TX mailbox. But the TX mailbox interrupt is using this same function
    to move the messages from ring buffer to empty TX mailboxes, so for that use case, there is this check */
    if(sendMB != true)
    {
      if( addToRingBuffer(txRing, CAN_tx_msg) == false )
      {
        ret = false; // no more room
      }
    }
    else { ret = false; }
  }
  __HAL_CAN_ENABLE_IT(n_pCanHandle, CAN_IT_TX_MAILBOX_EMPTY);
  return ret;
}

bool STM32_CAN::read(CAN_message_t &CAN_rx_msg)
{
  bool ret;
  __HAL_CAN_DISABLE_IT(n_pCanHandle, CAN_IT_RX_FIFO0_MSG_PENDING);
  ret = removeFromRingBuffer(rxRing, CAN_rx_msg);
  __HAL_CAN_ENABLE_IT(n_pCanHandle, CAN_IT_RX_FIFO0_MSG_PENDING);
  return ret;
}

bool STM32_CAN::setFilter(uint8_t bank_num, uint32_t filter_id, uint32_t mask, IDE std_ext, uint32_t filter_mode, uint32_t filter_scale, uint32_t fifo)
{
  CAN_FilterTypeDef sFilterConfig;

  sFilterConfig.FilterBank = bank_num;
  sFilterConfig.FilterMode = filter_mode;
  sFilterConfig.FilterScale = filter_scale;
  sFilterConfig.FilterFIFOAssignment = fifo;
  sFilterConfig.FilterActivation = ENABLE;

  if (std_ext == STD || (std_ext == AUTO && filter_id <= 0x7FF))
  {
    // Standard ID can be only 11 bits long
    sFilterConfig.FilterIdHigh = (uint16_t) (filter_id << 5);
    sFilterConfig.FilterIdLow = 0;
    sFilterConfig.FilterMaskIdHigh = (uint16_t) (mask << 5);
    sFilterConfig.FilterMaskIdLow = CAN_ID_EXT;
  }
  else
  {
    // Extended ID
    sFilterConfig.FilterIdLow = (uint16_t) (filter_id << 3);
    sFilterConfig.FilterIdLow |= CAN_ID_EXT;
    sFilterConfig.FilterIdHigh = (uint16_t) (filter_id >> 13);
    sFilterConfig.FilterMaskIdLow = (uint16_t) (mask << 3);
    sFilterConfig.FilterMaskIdLow |= CAN_ID_EXT;
    sFilterConfig.FilterMaskIdHigh = (uint16_t) (mask >> 13);
  }

  // Enable filter
  if (HAL_CAN_ConfigFilter( n_pCanHandle, &sFilterConfig ) != HAL_OK)
  {
    return 1;
  }
  else
  {
    return 0;
  }
}

void STM32_CAN::setMBFilter(CAN_BANK bank_num, CAN_FLTEN input)
{
  CAN_FilterTypeDef sFilterConfig;
  sFilterConfig.FilterBank = uint8_t(bank_num);
  if (input == ACCEPT_ALL) { sFilterConfig.FilterActivation = ENABLE; }
  else { sFilterConfig.FilterActivation = DISABLE; }

  HAL_CAN_ConfigFilter(n_pCanHandle, &sFilterConfig);
}

void STM32_CAN::setMBFilter(CAN_FLTEN input)
{
  CAN_FilterTypeDef sFilterConfig;
  uint8_t max_bank_num = 27;
  uint8_t min_bank_num = 0;
  #ifdef CAN2
  if (_canPort == CAN1){ max_bank_num = 13;}
  else if (_canPort == CAN2){ min_bank_num = 14;}
  #endif
  for (uint8_t bank_num = min_bank_num ; bank_num <= max_bank_num ; bank_num++)
  {
    sFilterConfig.FilterBank = bank_num;
    if (input == ACCEPT_ALL) { sFilterConfig.FilterActivation = ENABLE; }
    else { sFilterConfig.FilterActivation = DISABLE; }
    HAL_CAN_ConfigFilter(n_pCanHandle, &sFilterConfig);
  }
}

bool STM32_CAN::setMBFilterProcessing(CAN_BANK bank_num, uint32_t filter_id, uint32_t mask, IDE std_ext)
{
  // just convert the MB number enum to bank number.
  return setFilter(uint8_t(bank_num), filter_id, mask, std_ext);
}

bool STM32_CAN::setMBFilter(CAN_BANK bank_num, uint32_t id1, IDE std_ext)
{
  // by setting the mask to 0x1FFFFFFF we only filter the ID set as Filter ID.
  return setFilter(uint8_t(bank_num), id1, 0x1FFFFFFF, std_ext);
}

bool STM32_CAN::setMBFilter(CAN_BANK bank_num, uint32_t id1, uint32_t id2, IDE std_ext)
{
  // if we set the filter mode as IDLIST, the mask becomes filter ID too. So we can filter two totally independent IDs in same bank.
  return setFilter(uint8_t(bank_num), id1, id2, AUTO, CAN_FILTERMODE_IDLIST, std_ext);
}

// TBD, do this using "setFilter" -function
void STM32_CAN::initializeFilters()
{
  CAN_FilterTypeDef sFilterConfig;
  // We set first bank to accept all RX messages
  sFilterConfig.FilterBank = 0;
  sFilterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
  sFilterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
  sFilterConfig.FilterIdHigh = 0x0000;
  sFilterConfig.FilterIdLow = 0x0000;
  sFilterConfig.FilterMaskIdHigh = 0x0000;
  sFilterConfig.FilterMaskIdLow = 0x0000;
  sFilterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
  sFilterConfig.FilterActivation = ENABLE;
  #ifdef CAN2
  // Filter banks from 14 to 27 are for Can2, so first for Can2 is bank 14. This is not relevant for devices with only one CAN
  if (_canPort == CAN1)
  {
    sFilterConfig.SlaveStartFilterBank = 14;
  }
  if (_canPort == CAN2)
  {
    sFilterConfig.FilterBank = 14;
  }
  #endif

  HAL_CAN_ConfigFilter(n_pCanHandle, &sFilterConfig);
}

void STM32_CAN::initializeBuffers()
{
    if(isInitialized()) { return; }

    // set up the transmit and receive ring buffers
    if(tx_buffer==0)
    {
      tx_buffer=new CAN_message_t[sizeTxBuffer];
    }
    initRingBuffer(txRing, tx_buffer, sizeTxBuffer);

    if(rx_buffer==0)
    {
      rx_buffer=new CAN_message_t[sizeRxBuffer];
    }
    initRingBuffer(rxRing, rx_buffer, sizeRxBuffer);
}

void STM32_CAN::initRingBuffer(RingbufferTypeDef &ring, volatile CAN_message_t *buffer, uint32_t size)
{
    ring.buffer = buffer;
    ring.size = size;
    ring.head = 0;
    ring.tail = 0;
}

bool STM32_CAN::addToRingBuffer(RingbufferTypeDef &ring, const CAN_message_t &msg)
{
    uint16_t nextEntry;
    nextEntry =(ring.head + 1) % ring.size;

    // check if the ring buffer is full
    if(nextEntry == ring.tail)
	{
        return(false);
    }

    // add the element to the ring */
    memcpy((void *)&ring.buffer[ring.head],(void *)&msg, sizeof(CAN_message_t));

    // bump the head to point to the next free entry
    ring.head = nextEntry;

    return(true);
}
bool STM32_CAN::removeFromRingBuffer(RingbufferTypeDef &ring, CAN_message_t &msg)
{
    // check if the ring buffer has data available
    if(isRingBufferEmpty(ring) == true)
    {
        return(false);
    }

    // copy the message
    memcpy((void *)&msg,(void *)&ring.buffer[ring.tail], sizeof(CAN_message_t));

    // bump the tail pointer
    ring.tail =(ring.tail + 1) % ring.size;
    return(true);
}

bool STM32_CAN::isRingBufferEmpty(RingbufferTypeDef &ring)
{
    if(ring.head == ring.tail)
	{
        return(true);
    }

    return(false);
}

uint32_t STM32_CAN::ringBufferCount(RingbufferTypeDef &ring)
{
    int32_t entries;
    entries = ring.head - ring.tail;

    if(entries < 0)
    {
        entries += ring.size;
    }
    return((uint32_t)entries);
}

void STM32_CAN::setBaudRateValues(CAN_HandleTypeDef *CanHandle, uint16_t prescaler, uint8_t timeseg1,
                                                                uint8_t timeseg2, uint8_t sjw)
{
  uint32_t _SyncJumpWidth = 0;
  uint32_t _TimeSeg1 = 0;
  uint32_t _TimeSeg2 = 0;
  uint32_t _Prescaler = 0;

  /* the CAN specification (v2.0) states that SJW shall be programmable between
   * 1 and min(4, timeseg1)... the bxCAN documentation doesn't mention this
   */
  if (sjw > 4) sjw = 4;
  if (sjw > timeseg1) sjw = timeseg1;

  switch (sjw)
  {
    case 0:
    case 1:
      _SyncJumpWidth = CAN_SJW_1TQ;
      break;
    case 2:
      _SyncJumpWidth = CAN_SJW_2TQ;
      break;
    case 3:
      _SyncJumpWidth = CAN_SJW_3TQ;
      break;
    case 4:
    default: /* limit to 4 */
      _SyncJumpWidth = CAN_SJW_4TQ;
      break;
  }

  switch (timeseg1)
  {
    case 1:
      _TimeSeg1 = CAN_BS1_1TQ;
      break;
    case 2:
      _TimeSeg1 = CAN_BS1_2TQ;
      break;
    case 3:
      _TimeSeg1 = CAN_BS1_3TQ;
      break;
    case 4:
      _TimeSeg1 = CAN_BS1_4TQ;
      break;
    case 5:
      _TimeSeg1 = CAN_BS1_5TQ;
      break;
    case 6:
      _TimeSeg1 = CAN_BS1_6TQ;
      break;
    case 7:
      _TimeSeg1 = CAN_BS1_7TQ;
      break;
    case 8:
      _TimeSeg1 = CAN_BS1_8TQ;
      break;
    case 9:
      _TimeSeg1 = CAN_BS1_9TQ;
      break;
    case 10:
      _TimeSeg1 = CAN_BS1_10TQ;
      break;
    case 11:
      _TimeSeg1 = CAN_BS1_11TQ;
      break;
    case 12:
      _TimeSeg1 = CAN_BS1_12TQ;
      break;
    case 13:
      _TimeSeg1 = CAN_BS1_13TQ;
      break;
    case 14:
      _TimeSeg1 = CAN_BS1_14TQ;
      break;
    case 15:
      _TimeSeg1 = CAN_BS1_15TQ;
      break;
    case 16:
      _TimeSeg1 = CAN_BS1_16TQ;
      break;
    default:
      // should not happen
      _TimeSeg1 = CAN_BS1_1TQ;
      break;
  }

  switch (timeseg2)
  {
    case 1:
      _TimeSeg2 = CAN_BS2_1TQ;
      break;
    case 2:
      _TimeSeg2 = CAN_BS2_2TQ;
      break;
    case 3:
      _TimeSeg2 = CAN_BS2_3TQ;
      break;
    case 4:
      _TimeSeg2 = CAN_BS2_4TQ;
      break;
    case 5:
      _TimeSeg2 = CAN_BS2_5TQ;
      break;
    case 6:
      _TimeSeg2 = CAN_BS2_6TQ;
      break;
    case 7:
      _TimeSeg2 = CAN_BS2_7TQ;
      break;
    case 8:
      _TimeSeg2 = CAN_BS2_8TQ;
      break;
    default:
      // should not happen
      _TimeSeg2 = CAN_BS2_1TQ;
      break;
  }
  _Prescaler = prescaler;

  CanHandle->Init.SyncJumpWidth = _SyncJumpWidth;
  CanHandle->Init.TimeSeg1 = _TimeSeg1;
  CanHandle->Init.TimeSeg2 = _TimeSeg2;
  CanHandle->Init.Prescaler = _Prescaler;
}

template <typename T, size_t N>
bool STM32_CAN::lookupBaudrate(CAN_HandleTypeDef *CanHandle, int baud, const T(&table)[N]) {
  for (size_t i = 0; i < N; i++) {
    if (baud != (int)table[i].baudrate) {
      continue;
    }

    /* for the best chance at interoperability, use the widest SJW possible */
    setBaudRateValues(CanHandle, table[i].prescaler, table[i].timeseg1, table[i].timeseg2, 4);
    return true;
  }

  return false;
}

void STM32_CAN::calculateBaudrate(CAN_HandleTypeDef *CanHandle, int baud)
{
  uint8_t bs1;
  uint8_t bs2;
  uint16_t prescaler;

  const uint32_t frequency = getAPB1Clock();

  if (frequency == 48000000) {
    if (lookupBaudrate(CanHandle, baud, BAUD_RATE_TABLE_48M)) return;
  } else if (frequency == 45000000) {
    if (lookupBaudrate(CanHandle, baud, BAUD_RATE_TABLE_45M)) return;
  }

  /* this loop seeks a precise baudrate match, with the sample point positioned
   * at between ~75-95%. the nominal bit time is produced from N time quanta,
   * running at the prescaled clock rate (where N = 1 + bs1 + bs2). this algorithm
   * prefers the lowest prescaler (most time quanter per bit).
   *
   * many configuration sets can be discarded due to an out-of-bounds sample point,
   * or being unable to reach the desired baudrate.
   *
   * for the best chance at interoperability, we use the widest SJW possible.
   *
   * for more details + justification, see: https://github.com/pazi88/STM32_CAN/pull/41
   */
  for (prescaler = 1; prescaler <= 1024; prescaler += 1) {
    const uint32_t can_freq = frequency / prescaler;
    const uint32_t baud_min = can_freq / (1 + 5 + 16);

    /* skip all prescaler values that can't possibly achieve the desired baudrate */
    if (baud_min > baud) continue;

    for (bs2 = 1; bs2 <= 5; bs2 += 1) {
      for (bs1 = (bs2 * 3) - 1; bs1 <= 16; bs1 += 1) {
        const uint32_t baud_cur = can_freq / (1 + bs1 + bs2);

        if (baud_cur != baud) continue;

        setBaudRateValues(CanHandle, prescaler, bs1, bs2, 4);
        return;
      }
    }
  }

  /* uhoh, failed to calculate an acceptable baud rate... */
}

uint32_t STM32_CAN::getAPB1Clock()
{
  RCC_ClkInitTypeDef clkInit;
  uint32_t flashLatency;
  HAL_RCC_GetClockConfig(&clkInit, &flashLatency);

  uint32_t hclkClock = HAL_RCC_GetHCLKFreq();
  uint8_t clockDivider = 1;
  switch (clkInit.APB1CLKDivider)
  {
  case RCC_HCLK_DIV1:
    clockDivider = 1;
    break;
  case RCC_HCLK_DIV2:
    clockDivider = 2;
    break;
  case RCC_HCLK_DIV4:
    clockDivider = 4;
    break;
  case RCC_HCLK_DIV8:
    clockDivider = 8;
    break;
  case RCC_HCLK_DIV16:
    clockDivider = 16;
    break;
  default:
    // should not happen
    break;
  }

  uint32_t apb1Clock = hclkClock / clockDivider;

  return apb1Clock;
}

void STM32_CAN::enableMBInterrupts()
{
  if (n_pCanHandle->Instance == CAN1)
  {
    #ifdef CAN1_IRQn_AIO
    HAL_NVIC_EnableIRQ(CAN1_IRQn_AIO);
    #else
    HAL_NVIC_EnableIRQ(CAN1_TX_IRQn);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
    #endif /** else defined(CAN1_IRQn_AIO) */
  }
#ifdef CAN2
  else if (n_pCanHandle->Instance == CAN2)
  {
    HAL_NVIC_EnableIRQ(CAN2_TX_IRQn);
    HAL_NVIC_EnableIRQ(CAN2_RX0_IRQn);
  }
#endif
#ifdef CAN3
  else if (n_pCanHandle->Instance == CAN3)
  {
    HAL_NVIC_EnableIRQ(CAN3_TX_IRQn);
    HAL_NVIC_EnableIRQ(CAN3_RX0_IRQn);
  }
#endif
}

void STM32_CAN::disableMBInterrupts()
{
  if (n_pCanHandle->Instance == CAN1)
  {
    #ifdef CAN1_IRQn_AIO
    HAL_NVIC_DisableIRQ(CAN1_IRQn_AIO);
    #else
    HAL_NVIC_DisableIRQ(CAN1_TX_IRQn);
    HAL_NVIC_DisableIRQ(CAN1_RX0_IRQn);
    #endif /** else defined(CAN1_IRQn_AIO) */
  }
#ifdef CAN2
  else if (n_pCanHandle->Instance == CAN2)
  {
    HAL_NVIC_DisableIRQ(CAN2_TX_IRQn);
    HAL_NVIC_DisableIRQ(CAN2_RX0_IRQn);
  }
#endif
#ifdef CAN3
  else if (n_pCanHandle->Instance == CAN3)
  {
    HAL_NVIC_DisableIRQ(CAN3_TX_IRQn);
    HAL_NVIC_DisableIRQ(CAN3_RX0_IRQn);
  }
#endif
}

void STM32_CAN::setMode(Mode mode)
{
  this->mode = mode;
  n_pCanHandle->Init.Mode = mode;
}

void STM32_CAN::enableLoopBack( bool yes ) {
  setMode(yes ? Mode::LOOPBACK : Mode::NORMAL);
}

void STM32_CAN::enableSilentMode( bool yes ) {
  setMode(yes ? Mode::SILENT : Mode::NORMAL);
}

void STM32_CAN::enableSilentLoopBack( bool yes ) {
  setMode(yes ? Mode::SILENT_LOOPBACK : Mode::NORMAL);
}

void STM32_CAN::enableFIFO(bool status)
{
  //Nothing to do here. The FIFO is on by default. This is just to work with code made for Teensy FlexCan.
  (void) status;
}

/* Interrupt functions
-----------------------------------------------------------------------------------------------------------------------------------------------------------------
*/
// There is 3 TX mailboxes. Each one has own transmit complete callback function, that we use to pull next message from TX ringbuffer to be sent out in TX mailbox.
extern "C" void HAL_CAN_TxMailbox0CompleteCallback( CAN_HandleTypeDef *CanHandle )
{
  CAN_message_t txmsg;
  // use correct CAN instance
  if (CanHandle->Instance == CAN1) 
  {
    if (_CAN1->removeFromRingBuffer(_CAN1->txRing, txmsg))
    {
      _CAN1->write(txmsg, true);
    }
  }
#ifdef CAN2
  else if (CanHandle->Instance == CAN2) 
  {
    if (_CAN2->removeFromRingBuffer(_CAN2->txRing, txmsg))
    {
      _CAN2->write(txmsg, true);
    }
  }
#endif
#ifdef CAN3
  else if (CanHandle->Instance == CAN3) 
  {
    if (_CAN3->removeFromRingBuffer(_CAN3->txRing, txmsg))
    {
      _CAN3->write(txmsg, true);
    }
  }
#endif
}

extern "C" void HAL_CAN_TxMailbox1CompleteCallback( CAN_HandleTypeDef *CanHandle )
{
  CAN_message_t txmsg;
  // use correct CAN instance
  if (CanHandle->Instance == CAN1) 
  {
    if (_CAN1->removeFromRingBuffer(_CAN1->txRing, txmsg))
    {
      _CAN1->write(txmsg, true);
    }
  }
#ifdef CAN2
  else if (CanHandle->Instance == CAN2) 
  {
    if (_CAN2->removeFromRingBuffer(_CAN2->txRing, txmsg))
    {
      _CAN2->write(txmsg, true);
    }
  }
#endif
#ifdef CAN3
  else if (CanHandle->Instance == CAN3) 
  {
    if (_CAN3->removeFromRingBuffer(_CAN3->txRing, txmsg))
    {
      _CAN3->write(txmsg, true);
    }
  }
#endif
}

extern "C" void HAL_CAN_TxMailbox2CompleteCallback( CAN_HandleTypeDef *CanHandle )
{
  CAN_message_t txmsg;
  // use correct CAN instance
  if (CanHandle->Instance == CAN1) 
  {
    if (_CAN1->removeFromRingBuffer(_CAN1->txRing, txmsg))
    {
      _CAN1->write(txmsg, true);
    }
  }
#ifdef CAN2
  else if (CanHandle->Instance == CAN2) 
  {
    if (_CAN2->removeFromRingBuffer(_CAN2->txRing, txmsg))
    {
      _CAN2->write(txmsg, true);
    }
  }
#endif
#ifdef CAN3
  else if (CanHandle->Instance == CAN3) 
  {
    if (_CAN3->removeFromRingBuffer(_CAN3->txRing, txmsg))
    {
      _CAN3->write(txmsg, true);
    }
  }
#endif
}

// This is called by RX0_IRQHandler when there is message at RX FIFO0 buffer
extern "C" void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *CanHandle)
{
  CAN_message_t rxmsg;
  CAN_RxHeaderTypeDef   RxHeader;
  //bool state = Disable_Interrupts();

  // move the message from RX FIFO0 to RX ringbuffer
  if (HAL_CAN_GetRxMessage( CanHandle, CAN_RX_FIFO0, &RxHeader, rxmsg.buf ) == HAL_OK)
  {
    if ( RxHeader.IDE == CAN_ID_STD )
    {
      rxmsg.id = RxHeader.StdId;
      rxmsg.flags.extended = 0;
    }
    else
    {
      rxmsg.id = RxHeader.ExtId;
      rxmsg.flags.extended = 1;
    }

    rxmsg.flags.remote = RxHeader.RTR;
    rxmsg.mb           = RxHeader.FilterMatchIndex;
    rxmsg.timestamp    = RxHeader.Timestamp;
    rxmsg.len          = RxHeader.DLC;

    // use correct ring buffer based on CAN instance
    if (CanHandle->Instance == CAN1)
    {
      rxmsg.bus = 1;
      _CAN1->addToRingBuffer(_CAN1->rxRing, rxmsg);
    }
#ifdef CAN2
    else if (CanHandle->Instance == CAN2)
    {
      rxmsg.bus = 2;
      _CAN2->addToRingBuffer(_CAN2->rxRing, rxmsg);
    }
#endif
#ifdef CAN3
    else if (CanHandle->Instance == CAN3)
    {
      rxmsg.bus = 3;
      _CAN3->addToRingBuffer(_CAN3->rxRing, rxmsg);
    }
#endif
  }
  //Enable_Interrupts(state);
}

#ifdef CAN1_IRQHandler_AIO

extern "C" void CAN1_IRQHandler_AIO(void)
{
  HAL_CAN_IRQHandler(&hcan1);
}

#else

// RX IRQ handlers
extern "C" void CAN1_RX0_IRQHandler(void)
{
  HAL_CAN_IRQHandler(&hcan1);
}

#ifdef CAN2
extern "C" void CAN2_RX0_IRQHandler(void)
{
  HAL_CAN_IRQHandler(&hcan2);
}
#endif
#ifdef CAN3
extern "C" void CAN3_RX0_IRQHandler(void)
{
  HAL_CAN_IRQHandler(&hcan3);
}
#endif

// TX IRQ handlers
extern "C" void CAN1_TX_IRQHandler(void)
{
  HAL_CAN_IRQHandler(&hcan1);
}

#ifdef CAN2
extern "C" void CAN2_TX_IRQHandler(void)
{
  HAL_CAN_IRQHandler(&hcan2);
}
#endif
#ifdef CAN3
extern "C" void CAN3_TX_IRQHandler(void)
{
  HAL_CAN_IRQHandler(&hcan3);
}
#endif

#endif /* CAN1_IRQHandler_AIO */

