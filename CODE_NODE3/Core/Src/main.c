/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
I2C_HandleTypeDef hi2c1;

TIM_HandleTypeDef htim2;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_I2C1_Init(void);
static void MX_TIM2_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#define F411_I2C_ADDR  (0x20<<1) // Bắt buộc phải dịch trái 1 bit
/* USER CODE BEGIN 0 */
uint8_t int_Tem   = 0 ;
uint8_t float_Tem = 0 ;
uint8_t int_Hum   = 0 ;
uint8_t float_Hum = 0 ;
uint8_t CheckSum  = 0 ;


typedef struct {
    uint8_t physical_state;
    uint8_t confirmed_state;
    uint32_t last_time;
    uint8_t zero_count; // ← THÊM BỘ ĐẾM SỐ LẦN LIÊN TIẾP ĐỌC = 0
} Seat_t;

Seat_t seats[15]; // Mảng quản lý 14 cái ghế (12 nút nhấn + 2 ADC)
uint8_t flag_send_i2c = 0;   // Cờ báo hiệu cần gửi data lên F411
uint16_t all_seats_data = 0; // Biến 2 byte gom trạng thái các ghế

typedef struct {
    GPIO_TypeDef* port;
    uint16_t pin;
} Seat_Pin_Map_t;

// Đã đếm chuẩn 12 phần tử (Từ index 0 đến 11)
const Seat_Pin_Map_t SeatPins[15] = {
		{GPIOB,GPIO_PIN_11},//5 0
		{GPIOB,GPIO_PIN_1},  //1
		{GPIOB,GPIO_PIN_10},//0  2
		{GPIOB,GPIO_PIN_0},//7   3
		{GPIOA,GPIO_PIN_7},//9  4
		{GPIOA,GPIO_PIN_6},//8  5
		{GPIOA,GPIO_PIN_1},//11  6
		{GPIOA,GPIO_PIN_0},//10  7
		{GPIOB,GPIO_PIN_15},  //8
		{GPIOA,GPIO_PIN_8},//1  //9
		{GPIOA,GPIO_PIN_9},//2  10
		{GPIOA,GPIO_PIN_10},//4  11
		{GPIOC,GPIO_PIN_13},//12   12
		{GPIOC,GPIO_PIN_13},      //13
		{GPIOC,GPIO_PIN_13},//4  14


//		{GPIOB,GPIO_PIN_10},//0
//		{GPIOA,GPIO_PIN_8},//1
//		{GPIOA,GPIO_PIN_9},//2
//		{GPIOA,GPIO_PIN_10},//3
//		{GPIOA,GPIO_PIN_11},//4
//		//{GPIOA,GPIO_PIN_12},//5
//
//		//{GPIOB,GPIO_PIN_3},//7
//		{GPIOB,GPIO_PIN_13},//6
//		{GPIOB,GPIO_PIN_0},//7
//		{GPIOA,GPIO_PIN_6},//8
//		{GPIOA,GPIO_PIN_7},//9
//		{GPIOA,GPIO_PIN_0},//10
//		{GPIOA,GPIO_PIN_1},//11
//		{GPIOA,GPIO_PIN_15},//12
};

uint8_t Read_Seat_Hardware(uint8_t index) {
    // Chỉ đọc GPIO cho 12 ghế đầu tiên để tránh tràn mảng SeatPins
    if (index < 15) {
    	GPIO_TypeDef* current_port = SeatPins[index].port;
    	uint16_t current_pin = SeatPins[index].pin;
    	if (HAL_GPIO_ReadPin(current_port, current_pin) == GPIO_PIN_SET){
    		HAL_Delay(50);
    		return 1;
    	}
    	else if (HAL_GPIO_ReadPin(current_port, current_pin) == 0){
    		HAL_Delay(50);
    		return 0;
    	}
    }
    return 0;
}

//void Scan_All_Seats(void) {
//    // Quét đúng 15 ghế
//    for (int i = 0; i < 15; i++) {
//        uint8_t current_sensor = Read_Seat_Hardware(i);
//
//        if (current_sensor != seats[i].physical_state) {
//            // Trạng thái vật lý thay đổi -> cập nhật và reset mốc thời gian
//            seats[i].physical_state = current_sensor;
//            seats[i].last_time = HAL_GetTick();
//        }
//        else {
//            // Trạng thái vật lý đang giữ nguyên liên tục
//            uint32_t delay_thoi_gian;
//
//            // --- THIẾT LẬP THỜI GIAN CHỜ BẤT ĐỐI XỨNG ---
//            if (current_sensor == 1) {
//                // Người mới ngồi xuống: Chỉ cần giữ 0.5 giây là xác nhận có người
//                delay_thoi_gian = 1000;
//            } else {
//                // Người đứng lên (hoặc cảm biến tự ngắt): Chờ hẳn 3 giây mới xác nhận ghế trống
//                // Nếu trong 3 giây này tín hiệu bật lại mức 1, nó sẽ reset lại quá trình đếm
//                delay_thoi_gian = 30000;
//            }
//
//            // Nếu giữ trạng thái đủ thời gian yêu cầu
//            if (HAL_GetTick() - seats[i].last_time >= delay_thoi_gian) {
//
//                if (seats[i].confirmed_state != seats[i].physical_state) {
//                    seats[i].confirmed_state = seats[i].physical_state;
//
//                    if (seats[i].confirmed_state == 1) {
//                        all_seats_data |= (1 << i);  // Set bit lên 1 (Có người)
//                    } else {
//                        all_seats_data &= ~(1 << i); // Xóa bit về 0 (Trống)
//                    }
//
//                    flag_send_i2c = 1; // Bật cờ gửi data lên F4
//                }
//            }
//        }
//    }
//}
void Scan_All_Seats(void) {
    for (int i = 0; i < 15; i++) {
        uint8_t current_sensor = Read_Seat_Hardware(i);

        if (current_sensor != seats[i].physical_state) {
            // Trạng thái thay đổi → lưu lại và bắt đầu đếm 3 giây
            seats[i].physical_state = current_sensor;
            seats[i].last_time = HAL_GetTick();
        }
        else {
            // Giữ nguyên trạng thái → kiểm tra đủ 3 giây chưa
            if (HAL_GetTick() - seats[i].last_time >= 1000) {

                if (current_sensor == 1) {
                    // Giữ mức 1 đủ 3s → xác nhận có người
                    if (seats[i].confirmed_state != 1) {
                        seats[i].confirmed_state = 1;
                        all_seats_data |= (1 << i);
                        flag_send_i2c = 1;
                    }
                }
                else {
                    // Giữ mức 0 đủ 3s → xác nhận ghế trống
                    if (seats[i].confirmed_state != 0) {
                        seats[i].confirmed_state = 0;
                        all_seats_data &= ~(1 << i);
                        flag_send_i2c = 1;
                    }
                }
            }
        }
    }
}

uint8_t tx_buffer[4];
void xu_ly(uint16_t giatri){
	 // Tăng lên 3 byte
	tx_buffer[0] = 0x01;  // Gắn nhãn ID = 0x01 (Dữ liệu ghế)
	tx_buffer[1] = (giatri >> 8) & 0xFF; // Lấy byte cao
	tx_buffer[2] = giatri & 0xFF;        // Lấy byte thấp
	tx_buffer[3]= 0x09;
}
uint32_t doc = 0;
uint8_t trang_thai_cu = 1; // Trạng thái trước đó (mặc định nhả = 1)

void am_thanh(void) {
    uint8_t trang_thai_hien = HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_13);

    // Phát hiện nhấn xuống (cạnh xuống: 1 → 0)
    if (trang_thai_hien == 0 && trang_thai_cu == 1) {
        doc = HAL_GetTick(); // Lưu thời điểm bắt đầu nhấn
    }

    // Kiểm tra giữ đủ 1 giây
    if (trang_thai_hien == 0 && (HAL_GetTick() - doc >= 500)) {
        // Xử lý ở đây
    	HAL_GPIO_TogglePin(GPIOB, GPIO_PIN_12);
        doc = HAL_GetTick(); // Reset để không gọi liên tục
    }

    trang_thai_cu = trang_thai_hien;
}
//uint8_t data = 23;
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

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_TIM2_Init();
  /* USER CODE BEGIN 2 */
//  HAL_I2C_Master_Transmit(&hi2c1,(0x20<<1), &data, 1, 100);
  HAL_Delay(1000);
  HAL_StatusTypeDef trangthai;
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	 // HAL_Delay(2000);
    Scan_All_Seats();
    if (flag_send_i2c==1){

    	xu_ly(all_seats_data);
    	//HAL_Delay(20);
			trangthai = HAL_I2C_Master_Transmit(&hi2c1, F411_I2C_ADDR,(uint8_t*) &tx_buffer[0], 1, 1000);
			HAL_Delay(200);
			trangthai = HAL_I2C_Master_Transmit(&hi2c1, F411_I2C_ADDR,(uint8_t*) &tx_buffer[1], 1, 1000);
			HAL_Delay(200);
			trangthai = HAL_I2C_Master_Transmit(&hi2c1, F411_I2C_ADDR,(uint8_t*) &tx_buffer[2], 1, 1000);
			HAL_Delay(200);
			trangthai = HAL_I2C_Master_Transmit(&hi2c1, F411_I2C_ADDR,(uint8_t*) &tx_buffer[3], 1, 1000);
			flag_send_i2c =0;
    }
      am_thanh();

     	 HAL_Delay(200);


    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI_DIV2;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL15;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
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
  hi2c1.Init.OwnAddress1 = 0;
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
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 15;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA0 PA1 PA6 PA7
                           PA8 PA9 PA10 PA11
                           PA12 PA15 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_6|GPIO_PIN_7
                          |GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_12|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 PB10 PB11
                           PB15 PB3 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_15|GPIO_PIN_3;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PB12 */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PB13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

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
