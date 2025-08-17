from django.db import models


class Observation(models.Model):
    """
    Observation model represents a record of a bird observation.
    Attributes:
        created_at (DateTimeField): The timestamp when the observation was created. Automatically set to the current date and time when the object is created.
        video_file (FileField): The video file associated with the observation. Stored in the "videos/" directory.
        bird_species (CharField): The name of the bird species observed. Maximum length is 120 characters.
        confidence (FloatField): The confidence level of the bird species identification. Can be null or blank.
        frame_image (ImageField): An optional image frame extracted from the video. Stored in the "frames/" directory. Can be null or blank.
        notes (TextField): Additional notes or comments about the observation. Can be blank.
    Methods:
        __str__(): Returns a string representation of the observation, including the creation timestamp, bird species, and confidence level.
    """

    created_at = models.DateTimeField(auto_now_add=True)
    video_file = models.FileField(upload_to="videos/")
    bird_species = models.CharField(max_length=120)
    confidence = models.FloatField(null=True, blank=True)
    frame_image = models.ImageField(upload_to="frames/", null=True, blank=True)
    notes = models.TextField(blank=True)

    def __str__(self):
        return f"{self.created_at} - {self.bird_species} ({self.confidence or 0:.2f})"
