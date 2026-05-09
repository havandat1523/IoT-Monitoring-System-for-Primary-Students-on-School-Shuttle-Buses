/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "string.h"
#include "stdio.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

// =======================================================
// ĐỊNH NGHĨA CÁC TRẠNG THÁI CỦA HỆ THỐNG (STATE MACHINE)
// =======================================================
typedef enum {
    STATE_INIT,      // Trạng thái khởi động, chờ các module ổn định
    STATE_NORMAL,    // Trạng thái an toàn, giám sát bình thường
    STATE_ALARM      // Trạng thái có lỗi an toàn (Tài xế vắng mặt hoặc chưa quẹt thẻ)
} BusState_t;

BusState_t sysState = STATE_INIT; // Trạng thái mặc định ban đầu

uint8_t u8_RxBuff1[50];  // Buffer cho UART1 (Camera)
uint8_t u8_RxBuff2[50];  // Buffer cho UART2 (ESP32)
uint8_t u8_RxBuff3[50];  // Buffer cho UART6 (Loa/RFID)

uint8_t u8_RxData1;      // Byte nhan cua esp
uint8_t u8_RxData2;      // Byte nhan cua Camera
uint8_t u8_RxData3;      // Byte nhan cua Loa/RFID

uint8_t _rxIndex1 = 0;   // Index cho UART1
uint8_t _rxIndex2 = 0;   // Index cho UART2
uint8_t _rxIndex3 = 0;   // Index cho UART6

volatile uint8_t Tx_Flag1 = 0; 
volatile uint8_t Tx_Flag2 = 0; 
volatile uint8_t Tx_Flag3 = 0;

uint8_t rfid_count = 0;         // Dem so luong hoc sinh quet the
uint8_t seats[16] = {0};        // Trang thai ghe: 1 = co nguoi, 0 = trong
uint8_t people_detect = 0;      // Cam bien phat hien nguoi: 1 = co, 0 = khong
uint8_t is_goingtoschool = 0;   // 0 là di dón hoc sinh , 1 mang hs den truong

// Biến lưu trạng thái lỗi để State Machine quyết định
uint8_t err_driver = 0;
uint8_t err_rfid = 0;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart6;

/* USER CODE BEGIN PV */
char* ghe_ngoi[16] = {"m0\n","m1\n","m2\n","m3\n","m4\n","m5\n","m6\n","m7\n","m8\n","m9\n","m10\n","m11\n","m12\n","x13\n","x14\n","x15\n"};
char* ghe_k_ngoi[16] = {"x0\n","x1\n","x2\n","x3\n","x4\n","x5\n","x6\n","x7\n","x8\n","x9\n","x10\n","x11\n","x12\n","x13\n","x14\n","x15\n"};

uint16_t f411_seats_data;    // Biến 16-bit lưu trạng thái của 16 ghế
uint8_t flag_new_data = 0;
uint8_t rx_buffer[2];
uint8_t count =0;
uint8_t giatri =0;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_I2C1_Init(void);
static void MX_USART6_UART_Init(void);

/* USER CODE BEGIN PFP */
void send_tb() {
    char jsonBuf[256];
    uint8_t hs_count = 0; 
    
    for(int i = 1; i <= 15; i++) {
        if(seats[i] == 1) hs_count++;
    }

    uint8_t isAlarm = (sysState == STATE_ALARM) ? 1 : 0;

    sprintf(jsonBuf, "TB:{\"so_hs_tren_xe\":%d, \"tai_xe_tren_xe\":%s, \"ghe_co_nguoi\":%s, \"bao_dong\":%s}\r\n",
            hs_count,
            (seats[0] == 1) ? "true" : "false",
            (hs_count > 0) ? "true" : "false",
            (isAlarm == 1) ? "true" : "false");
            
    HAL_UART_Transmit(&huart2, (uint8_t*)jsonBuf, strlen(jsonBuf), 50);
}

// Hàm này chỉ làm 1 việc duy nhất: Đọc tình huống hiện tại và gán lỗi
void Check_System_Errors() {
    uint8_t hs_count = 0;
    for(int i = 1; i <= 15; i++) {
        if(seats[i] == 1) hs_count++;
    }
    if(hs_count ==0 && is_goingtoschool == 1){
		 if(seats[0]==1){
		  HAL_UART_Transmit(&huart6, (uint8_t*)"001\r\n", strlen("001\r\n"), 50);
		 }
		}
    err_driver = ((seats[0] == 0) && (hs_count > 0 || people_detect == 1));
    err_rfid = (hs_count > rfid_count);
}

// Hàm gom khối xử lý I2C
void Process_I2C_Data() {
    if (flag_new_data == 1) {
        flag_new_data = 0;
        f411_seats_data = (rx_buffer[0] << 8) | rx_buffer[1];
        
        //char debug_buf[50];
        //sprintf(debug_buf, "I2C RAW: %02X %02X -> %d\r\n", rx_buffer[0], rx_buffer[1], f411_seats_data);
        //HAL_UART_Transmit(&huart2, (uint8_t*)debug_buf, strlen(debug_buf), 50);
        
        uint8_t is_changed = 0;
        for (int i = 0; i < 16; i++) {
            uint8_t bit_val = (f411_seats_data & (1 << i)) ? 1 : 0;
            if (seats[i] != bit_val) {
                seats[i] = bit_val; 
                is_changed = 1;     
                if (seats[i] == 1) {
                    HAL_UART_Transmit(&huart1, (uint8_t*)ghe_ngoi[i], strlen(ghe_ngoi[i]), 50);
                } else {
                    HAL_UART_Transmit(&huart1, (uint8_t*)ghe_k_ngoi[i], strlen(ghe_k_ngoi[i]), 50);
                }
            }
        }
        
        if (is_changed) {
            Check_System_Errors(); // Đánh giá lại lỗi
            send_tb();             // Báo cáo lên web
        }
    }
}

// Hàm gom khối xử lý phản hồi từ ESP32
void Process_ESP_Responses() {
    if (Tx_Flag1 == 1) {
        if(strncmp((char*)u8_RxBuff2, "GS:", 3) == 0) {
            char* response = (char*)u8_RxBuff2 + 3;
            uint8_t needUpdateTB = 0;

            if (strncmp(response, "M_OK", 4) == 0 || strncmp(response, "M_END", 5) == 0) {
                if(strncmp(response, "M_END", 5) == 0){
    if(seats[0] == 1) {  // Tài xế đã lên xe
        HAL_UART_Transmit(&huart6, (uint8_t*)"003\r\n", strlen("003\r\n"), 50);
        is_goingtoschool = 1;
    } else {
        // ⚠️ Tài xế vắng mặt - không gửi 003, đợi tài xế lên xe
        //HAL_UART_Transmit(&huart6, (uint8_t*)"002\r\n", strlen("002\r\n"), 50); // Cảnh báo tài xế vắng mặt
    }
}  
                rfid_count++;
                needUpdateTB = 1;
            } 
            else if (strncmp(response, "A_OK", 4) == 0 || strncmp(response, "A_END", 5) == 0 || strncmp(response, "A_C", 3) == 0) {
                if(rfid_count > 0) rfid_count--;
                needUpdateTB = 1;
                
                if (rfid_count == 0) {
                    HAL_UART_Transmit(&huart6, (uint8_t*)"001\r\n", strlen("001\r\n"), 50);
                }
            }
            else if (strncmp(response, "NOT_FOUND", 9) == 0) {
                 HAL_UART_Transmit(&huart6, (uint8_t*)"010\r\n", strlen("010\r\n"), 50);
            }

            Check_System_Errors();
            if(needUpdateTB) send_tb();
        }
        memset(u8_RxBuff2, 0, sizeof(u8_RxBuff2));
        _rxIndex2 = 0;
        Tx_Flag1 = 0;
    }
}

// Hàm gom khối xử lý Camera
void Process_Camera_Data() {
    if (Tx_Flag2 == 1) {
        if(u8_RxBuff1[0] == '1') people_detect = 1;
        else if(u8_RxBuff1[0] == '0') people_detect = 0;

        Check_System_Errors();
        send_tb();

        memset(u8_RxBuff1, 0, sizeof(u8_RxBuff1));
        _rxIndex1 = 0;
        Tx_Flag2 = 0;
    }
}

// Hàm gom khối xử lý RFID
void Process_RFID_Data() {
    if (Tx_Flag3 == 1) {
        char rfidCmd[50];
        sprintf(rfidCmd, "RFID:%s\r\n", u8_RxBuff3);
        HAL_UART_Transmit(&huart2, (uint8_t*)rfidCmd, strlen(rfidCmd), 50);
        
        memset(u8_RxBuff3, 0, sizeof(u8_RxBuff3));
        _rxIndex3 = 0;
        Tx_Flag3 = 0;
    }
}
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART1) HAL_UART_Receive_IT(&huart1, &u8_RxData2, 1);
    else if (huart->Instance == USART2) HAL_UART_Receive_IT(&huart2, &u8_RxData1, 1);
    else if (huart->Instance == USART6) HAL_UART_Receive_IT(&huart6, &u8_RxData3, 1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if(huart->Instance == USART2) {
        if(u8_RxData1 != '\r' && u8_RxData1 != '\n') {
            u8_RxBuff2[_rxIndex2++] = u8_RxData1;
            if(_rxIndex2 >= sizeof(u8_RxBuff2) - 1) _rxIndex2 = sizeof(u8_RxBuff2) - 1;
        } else {
            if(_rxIndex2 > 0) { u8_RxBuff2[_rxIndex2] = '\0'; Tx_Flag1 = 1; }
        }
        HAL_UART_Receive_IT(&huart2, &u8_RxData1, 1);
    }
    
    if(huart->Instance == USART1) {
        if(u8_RxData2 != '\r' && u8_RxData2 != '\n') {
            u8_RxBuff1[_rxIndex1++] = u8_RxData2;
            if(_rxIndex1 >= sizeof(u8_RxBuff1) - 1) _rxIndex1 = sizeof(u8_RxBuff1) - 1;
        } else {
            if(_rxIndex1 > 0) { u8_RxBuff1[_rxIndex1] = '\0'; Tx_Flag2 = 1; }
        }
        HAL_UART_Receive_IT(&huart1, &u8_RxData2, 1); 
    }
        
    if(huart->Instance == USART6) {
        if(u8_RxData3 != '\r' && u8_RxData3 != '\n') {
            u8_RxBuff3[_rxIndex3++] = u8_RxData3;
            if(_rxIndex3 >= sizeof(u8_RxBuff3) - 1) _rxIndex3 = sizeof(u8_RxBuff3) - 1;
        } else {
            if(_rxIndex3 > 0) { u8_RxBuff3[_rxIndex3] = '\0'; Tx_Flag3 = 1; }
        }
        HAL_UART_Receive_IT(&huart6, &u8_RxData3, 1);
    }
}

void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c->Instance == I2C1) {
        if ((count == 0) && (giatri == 0x01)) count = 1;
        else if (count == 1) { rx_buffer[0] = giatri; count = 2; } 
        else if (count == 2) { rx_buffer[1] = giatri; count = 3; } 
        else if ((count == 3) && (giatri == 0x09)) { count = 0; flag_new_data = 1; } 
        else count = 0; 
        HAL_I2C_Slave_Receive_IT(&hi2c1, (uint8_t*)&giatri, 1);
    }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c->Instance == I2C1) {
        HAL_I2C_DeInit(&hi2c1);
        HAL_I2C_Init(&hi2c1);
        count = 0;
        HAL_I2C_Slave_Receive_IT(&hi2c1, (uint8_t*)&giatri, 1);
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/
  HAL_Init();
  SystemClock_Config();
  MX_GPIO_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  MX_I2C1_Init();
  MX_USART6_UART_Init();

  /* USER CODE BEGIN 2 */
  HAL_UART_Receive_IT(&huart2, &u8_RxData1, 1);
  HAL_UART_Receive_IT(&huart1, &u8_RxData2, 1);
  HAL_UART_Receive_IT(&huart6, &u8_RxData3, 1);
  HAL_I2C_Slave_Receive_IT(&hi2c1,&giatri, 1); 
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  
  // Thời gian lưu lần cuối cùng gửi dữ liệu định kỳ
  uint32_t last_send_time = 0; 
  static uint8_t last_err_driver = 0;
  static uint8_t last_err_rfid = 0;
  
  while(1){
      // 1. LUÔN LUÔN TIẾP NHẬN DỮ LIỆU TỪ CÁC CẢM BIẾN (Chạy bất kể trạng thái nào)
      Process_I2C_Data();
      Process_Camera_Data();
      Process_ESP_Responses();
      Process_RFID_Data();

      // 2. MÁY TRẠNG THÁI (STATE MACHINE) ĐỂ XỬ LÝ LOGIC AN TOÀN
       // 2. MÁY TRẠNG THÁI (STATE MACHINE) ĐỂ XỬ LÝ LOGIC AN TOÀN
      switch (sysState) {
          
          case STATE_INIT:
              // Chờ 2 giây để các module khởi động xong, sau đó vào trạng thái Bình thường
              if (HAL_GetTick() > 2000) {
                  sysState = STATE_NORMAL;
              }
              break;

          case STATE_NORMAL:
              // Nếu xuất hiện bất kỳ lỗi nào, chuyển sang ALARM
              if (err_driver == 1 || err_rfid == 1) {
                  sysState = STATE_ALARM; 
                  send_tb(); // Gửi trạng thái lên web
              }
              break;

          case STATE_ALARM:
              // Đang báo động. Nếu TẤT CẢ các lỗi đã được xử lý -> Trở về Bình thường
              if (err_driver == 0 && err_rfid == 0) {
                  sysState = STATE_NORMAL; 
                  send_tb(); // Gửi trạng thái lên web
              }
              break;
      }

      // XỬ LÝ GỬI UART CẢNH BÁO (ĐỘC LẬP VỚI STATE MACHINE)
      if (sysState != STATE_INIT) {
          // Nếu phát hiện lỗi tài xế MỚI XUẤT HIỆN
          if (err_driver == 1 && last_err_driver == 0) {
              HAL_UART_Transmit(&huart2, (uint8_t*)"sos\r\n", strlen("sos\r\n"), 50); // SOS
          }
          // Nếu phát hiện lỗi RFID MỚI XUẤT HIỆN
          if (err_rfid == 1 && last_err_rfid == 0) {
              HAL_UART_Transmit(&huart6, (uint8_t*)"005\r\n", strlen("005\r\n"), 50); // Nhắc quẹt thẻ
          }
          
          // Cập nhật lại lịch sử lỗi
          last_err_driver = err_driver;
          last_err_rfid = err_rfid;
      }

      // 3. TÁC VỤ ĐỊNH KỲ: Gửi dữ liệu lên web mỗi 5 giây để duy trì kết nối
      if (sysState != STATE_INIT && (HAL_GetTick() - last_send_time >= 5000)) {
          send_tb();
          last_send_time = HAL_GetTick();
      }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 84;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.ClockSpeed = 100000;
  hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
  hi2c1.Init.OwnAddress1 = 64;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 9600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{

  /* USER CODE BEGIN USART2_Init 0 */

  /* USER CODE END USART2_Init 0 */

  /* USER CODE BEGIN USART2_Init 1 */

  /* USER CODE END USART2_Init 1 */
  huart2.Instance = USART2;
  huart2.Init.BaudRate = 115200;
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits = UART_STOPBITS_1;
  huart2.Init.Parity = UART_PARITY_NONE;
  huart2.Init.Mode = UART_MODE_TX_RX;
  huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART2_Init 2 */

  /* USER CODE END USART2_Init 2 */

}

/**
  * @brief USART6 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART6_UART_Init(void)
{

  /* USER CODE BEGIN USART6_Init 0 */

  /* USER CODE END USART6_Init 0 */

  /* USER CODE BEGIN USART6_Init 1 */

  /* USER CODE END USART6_Init 1 */
  huart6.Instance = USART6;
  huart6.Init.BaudRate = 9600;
  huart6.Init.WordLength = UART_WORDLENGTH_8B;
  huart6.Init.StopBits = UART_STOPBITS_1;
  huart6.Init.Parity = UART_PARITY_NONE;
  huart6.Init.Mode = UART_MODE_TX_RX;
  huart6.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart6.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart6) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART6_Init 2 */

  /* USER CODE END USART6_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
