#pragma once
/// \file hba.h
/// Definition for AHCI host bus adapters
#include "kutil/vector.h"
#include "ahci/port.h"

class pci_device;


namespace ahci {

enum class hba_cap : uint32_t;
enum class hba_cap2 : uint32_t;
struct hba_data;


/// An AHCI host bus adapter
class hba
{
public:
	/// Constructor.
	/// \arg device  The PCI device for this HBA
	hba(pci_device *device);

private:
	pci_device *m_device;
	hba_data *m_data;
	kutil::vector<port> m_ports;
};

} // namespace ahci
