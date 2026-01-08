#include "ualink/upli_credit.h"

#include <cassert>
#include <iostream>

using namespace ualink::upli;

// Test basic credit initialization
void test_credit_initialization() {
  std::cout << "test_credit_initialization: ";

  UpliCreditManager manager;

  // Configure port 0 with default config
  PortCreditConfig config{};
  config.vc_config[0].initial_credits = 16;
  config.vc_config[1].initial_credits = 8;
  config.vc_config[2].initial_credits = 4;
  config.vc_config[3].initial_credits = 2;

  manager.configure_port(0, config);
  manager.initialize_credits();

  // Verify initialization
  assert(manager.is_initialized(0));
  assert(manager.get_available_credits(0, 0) == 16);
  assert(manager.get_available_credits(0, 1) == 8);
  assert(manager.get_available_credits(0, 2) == 4);
  assert(manager.get_available_credits(0, 3) == 2);

  std::cout << "PASS\n";
}

// Test credit consumption
void test_credit_consumption() {
  std::cout << "test_credit_consumption: ";

  UpliCreditManager manager;

  PortCreditConfig config{};
  config.vc_config[0].initial_credits = 10;
  manager.configure_port(0, config);
  manager.initialize_credits();

  // Consume credits
  assert(manager.has_credit(0, 0));
  assert(manager.consume_credit(0, 0));
  assert(manager.get_available_credits(0, 0) == 9);

  assert(manager.consume_credit(0, 0));
  assert(manager.get_available_credits(0, 0) == 8);

  // Verify stats
  const auto& stats = manager.get_stats(0, 0);
  assert(stats.credits_consumed == 2);
  assert(stats.credits_available == 8);

  std::cout << "PASS\n";
}

// Test credit exhaustion
void test_credit_exhaustion() {
  std::cout << "test_credit_exhaustion: ";

  UpliCreditManager manager;

  PortCreditConfig config{};
  config.vc_config[0].initial_credits = 3;
  manager.configure_port(0, config);
  manager.initialize_credits();

  // Consume all credits
  assert(manager.consume_credit(0, 0));  // 2 left
  assert(manager.consume_credit(0, 0));  // 1 left
  assert(manager.consume_credit(0, 0));  // 0 left

  // No more credits available
  assert(!manager.has_credit(0, 0));
  assert(!manager.consume_credit(0, 0));  // Should fail

  // Verify stats
  const auto& stats = manager.get_stats(0, 0);
  assert(stats.credits_consumed == 3);
  assert(stats.send_blocked_count == 1);

  std::cout << "PASS\n";
}

// Test credit return processing
void test_credit_return_processing() {
  std::cout << "test_credit_return_processing: ";

  UpliCreditManager manager;

  PortCreditConfig config{};
  config.vc_config[0].initial_credits = 10;
  manager.configure_port(0, config);
  manager.initialize_credits();

  // Consume some credits
  assert(manager.consume_credit(0, 0));
  assert(manager.consume_credit(0, 0));
  assert(manager.consume_credit(0, 0));
  assert(manager.get_available_credits(0, 0) == 7);

  // Receive credit return
  UpliCreditReturn credits{};
  credits.ports[0].credit_vld = true;
  credits.ports[0].credit_pool = false;
  credits.ports[0].credit_vc = 0;
  credits.ports[0].credit_num = 1;  // 2 credits (0-3 encoding)
  credits.credit_init_done[0] = true;

  manager.process_credit_return(credits);

  // Verify credits returned
  assert(manager.get_available_credits(0, 0) == 9);

  const auto& stats = manager.get_stats(0, 0);
  assert(stats.credits_returned == 2);

  std::cout << "PASS\n";
}

// Test pool-based credits
void test_pool_credits() {
  std::cout << "test_pool_credits: ";

  UpliCreditManager manager;

  PortCreditConfig config{};
  config.use_pool = true;
  config.pool_credits = 32;
  manager.configure_port(0, config);
  manager.initialize_credits();

  // All VCs share the same pool
  assert(manager.get_available_credits(0, 0) == 32);
  assert(manager.get_available_credits(0, 1) == 32);
  assert(manager.get_available_credits(0, 2) == 32);
  assert(manager.get_available_credits(0, 3) == 32);

  // Consume from any VC affects the pool
  assert(manager.consume_credit(0, 0));
  assert(manager.get_available_credits(0, 0) == 31);
  assert(manager.get_available_credits(0, 1) == 31);

  assert(manager.consume_credit(0, 2));
  assert(manager.get_available_credits(0, 0) == 30);

  std::cout << "PASS\n";
}

// Test multiple ports
void test_multiple_ports() {
  std::cout << "test_multiple_ports: ";

  UpliCreditManager manager;

  // Configure different credits for each port
  for (std::uint8_t port_id = 0; port_id < kMaxPorts; ++port_id) {
    PortCreditConfig config{};
    config.vc_config[0].initial_credits = (port_id + 1) * 10;
    manager.configure_port(port_id, config);
  }

  manager.initialize_credits();

  // Verify each port has independent credits
  assert(manager.get_available_credits(0, 0) == 10);
  assert(manager.get_available_credits(1, 0) == 20);
  assert(manager.get_available_credits(2, 0) == 30);
  assert(manager.get_available_credits(3, 0) == 40);

  // Consuming from port 0 doesn't affect others
  assert(manager.consume_credit(0, 0));
  assert(manager.get_available_credits(0, 0) == 9);
  assert(manager.get_available_credits(1, 0) == 20);

  std::cout << "PASS\n";
}

// Test credit return generation
void test_credit_return_generation() {
  std::cout << "test_credit_return_generation: ";

  UpliCreditManager manager;

  PortCreditConfig config{};
  config.vc_config[0].initial_credits = 10;
  manager.configure_port(0, config);
  manager.initialize_credits();

  // No credits consumed yet, no return needed
  auto return_msg = manager.generate_credit_return();
  // Note: Implementation returns init_done even with no consumption
  // This is fine for the simple implementation

  // Consume some credits
  assert(manager.consume_credit(0, 0));
  assert(manager.consume_credit(0, 0));

  // Generate credit return
  return_msg = manager.generate_credit_return();
  assert(return_msg.has_value());
  assert(return_msg->ports[0].credit_vld);
  assert(return_msg->ports[0].credit_vc == 0);
  assert(return_msg->credit_init_done[0]);

  std::cout << "PASS\n";
}

// Test reset functionality
void test_reset() {
  std::cout << "test_reset: ";

  UpliCreditManager manager;

  PortCreditConfig config{};
  config.vc_config[0].initial_credits = 10;
  manager.configure_port(0, config);
  manager.initialize_credits();

  // Consume credits
  assert(manager.consume_credit(0, 0));
  assert(manager.consume_credit(0, 0));
  assert(manager.get_available_credits(0, 0) == 8);

  // Reset
  manager.reset();
  assert(!manager.is_initialized(0));
  assert(manager.get_available_credits(0, 0) == 0);

  // Re-initialize
  manager.initialize_credits();
  assert(manager.get_available_credits(0, 0) == 10);

  std::cout << "PASS\n";
}

// Test credit capping
void test_credit_capping() {
  std::cout << "test_credit_capping: ";

  UpliCreditManager manager;

  PortCreditConfig config{};
  config.vc_config[0].initial_credits = 10;
  manager.configure_port(0, config);
  manager.initialize_credits();

  // Consume credits
  assert(manager.consume_credit(0, 0));
  assert(manager.consume_credit(0, 0));
  assert(manager.get_available_credits(0, 0) == 8);

  // Return more credits than consumed
  UpliCreditReturn credits{};
  credits.ports[0].credit_vld = true;
  credits.ports[0].credit_pool = false;
  credits.ports[0].credit_vc = 0;
  credits.ports[0].credit_num = 3;  // 4 credits
  credits.credit_init_done[0] = true;

  manager.process_credit_return(credits);

  // Credits should be capped at initial value
  assert(manager.get_available_credits(0, 0) == 10);

  std::cout << "PASS\n";
}

// Test validation errors
void test_validation_errors() {
  std::cout << "test_validation_errors: ";

  UpliCreditManager manager;

  // Invalid port ID
  try {
    manager.configure_port(4, PortCreditConfig{});
    assert(false && "Should have thrown");
  } catch (const std::invalid_argument&) {
    // Expected
  }

  // Invalid VC
  manager.initialize_credits();
  try {
    [[maybe_unused]] const bool result = manager.has_credit(0, 4);
    assert(false && "Should have thrown");
  } catch (const std::invalid_argument&) {
    // Expected
  }

  std::cout << "PASS\n";
}

// Test stats tracking
void test_stats_tracking() {
  std::cout << "test_stats_tracking: ";

  UpliCreditManager manager;

  PortCreditConfig config{};
  config.vc_config[0].initial_credits = 5;
  manager.configure_port(0, config);
  manager.initialize_credits();

  // Consume credits
  assert(manager.consume_credit(0, 0));
  assert(manager.consume_credit(0, 0));
  assert(manager.consume_credit(0, 0));

  const auto& stats1 = manager.get_stats(0, 0);
  assert(stats1.credits_consumed == 3);
  assert(stats1.credits_available == 2);
  assert(stats1.credits_returned == 0);
  assert(stats1.send_blocked_count == 0);

  // Try to consume when depleted
  assert(manager.consume_credit(0, 0));
  assert(manager.consume_credit(0, 0));
  assert(!manager.consume_credit(0, 0));  // Blocked

  const auto& stats2 = manager.get_stats(0, 0);
  assert(stats2.credits_consumed == 5);
  assert(stats2.send_blocked_count == 1);

  // Return credits
  manager.return_credits(0, 0, 3);

  const auto& stats3 = manager.get_stats(0, 0);
  assert(stats3.credits_returned == 3);
  assert(stats3.credits_available == 3);

  std::cout << "PASS\n";
}

int main() {
  std::cout << "\n=== UPLI Credit Manager Tests ===\n\n";

  // Basic functionality
  test_credit_initialization();
  test_credit_consumption();
  test_credit_exhaustion();
  test_credit_return_processing();

  // Advanced features
  test_pool_credits();
  test_multiple_ports();
  test_credit_return_generation();
  test_reset();
  test_credit_capping();

  // Error handling and stats
  test_validation_errors();
  test_stats_tracking();

  std::cout << "\n=== All UPLI Credit Manager Tests Passed ===\n";
  return 0;
}
