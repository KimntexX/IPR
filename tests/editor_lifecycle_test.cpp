#include "imageeditor.h"
#include "imageprovider.h"
#include <QCoreApplication>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ImageProvider provider;
    // No worker has started. Destruction must not wait on an empty future.
    ImageEditor editor(&provider);
    return 0;
}
