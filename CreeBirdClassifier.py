import torch
import torch.nn as nn
from torchvision import models

num_classes = 10  # adapter
model = models.mobilenet_v3_small(weights="DEFAULT")
model.classifier[3] = nn.Linear(model.classifier[3].in_features, num_classes)
# charger vos poids fine-tunés ici: model.load_state_dict(torch.load("ckpt.pt"))
model.eval()

dummy = torch.randn(1, 3, 224, 224)
torch.onnx.export(
    model,
    dummy,
    "bird_classifier.onnx",
    input_names=["input"],
    output_names=["logits"],
    opset_version=17,
    dynamic_axes=None,
)
